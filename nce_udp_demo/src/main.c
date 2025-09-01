/******************************************************************************
 * @file       main.c
 * @brief      1NCE UDP Demo
 * @details    This file contains the main function, initialization, and implementation for
 *             communicating with 1NCE endpoints via the UDP protocol. It supports sending
 *             compressed payloads using the Energy Saver feature.
 *
 * @copyright
 *     Portions copyright (c) 2019 Nordic Semiconductor ASA
 *     Modified by 1NCE GmbH (c) 2024
 * @date       2025-05
 ******************************************************************************/

// SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

/******************************************************************************
* Includes
******************************************************************************/

#include <zephyr/kernel.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/net/socket.h>
#include <nce_iot_c_sdk.h>
#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    #include <zephyr/drivers/gpio.h>

/*
 * Thingy:91 LEDs
 */
static struct gpio_dt_spec ledRed = GPIO_DT_SPEC_GET_OR( DT_ALIAS( led0 ), gpios,
                                                         { 0 } );
static struct gpio_dt_spec ledGreen = GPIO_DT_SPEC_GET_OR( DT_ALIAS( led1 ), gpios,
                                                           { 0 } );
static struct gpio_dt_spec ledBlue = GPIO_DT_SPEC_GET_OR( DT_ALIAS( led2 ), gpios,
                                                          { 0 } );
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

/******************************************************************************
* Macros and Constants
******************************************************************************/

/* LOG Macros */
LOG_MODULE_REGISTER( NCE_UDP_DEMO, CONFIG_LOG_DEFAULT_LEVEL );
#define UDP_IP_HEADER_SIZE    28
#define UPLINK_STACK_SIZE     2048
#define THREAD_PRIORITY       5

/******************************************************************************
* Static Variables
******************************************************************************/

/** @brief Kernel stack and threading configurations */
K_THREAD_STACK_DEFINE( uplink_thread_stack, UPLINK_STACK_SIZE );
struct k_thread uplink_thread;
static int uplink_fd = -1;
static K_SEM_DEFINE( lte_connected_sem, 0, 1 );

#if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER )
    #define DOWNLINK_STACK_SIZE    1024
K_THREAD_STACK_DEFINE( downlink_thread_stack, DOWNLINK_STACK_SIZE );
struct k_thread downlink_thread;
static int downlink_fd = -1;
#endif

/******************************************************************************
* Functions
******************************************************************************/
#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )

/**
 * @brief Configures the LED GPIOs if the device is ready.
 */
void configureLeds()
{
    int ret = 0;

    if( ledRed.port && !device_is_ready( ledRed.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it",
                 ret, ledRed.port->name );
        ledRed.port = NULL;
    }

    if( ledRed.port )
    {
        ret = gpio_pin_configure_dt( &ledRed, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d",
                     ret, ledRed.port->name, ledRed.pin );
            ledRed.port = NULL;
        }
    }

    if( ledGreen.port && !device_is_ready( ledGreen.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it",
                 ret, ledGreen.port->name );
        ledGreen.port = NULL;
    }

    if( ledGreen.port )
    {
        ret = gpio_pin_configure_dt( &ledGreen, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d",
                     ret, ledGreen.port->name, ledGreen.pin );
            ledGreen.port = NULL;
        }
    }

    if( ledBlue.port && !device_is_ready( ledBlue.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it",
                 ret, ledBlue.port->name );
        ledBlue.port = NULL;
    }

    if( ledBlue.port )
    {
        ret = gpio_pin_configure_dt( &ledBlue, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d",
                     ret, ledBlue.port->name, ledBlue.pin );
            ledBlue.port = NULL;
        }
    }
}
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

/**
 * @brief Handles LTE network events.
 *
 * @param evt Pointer to the LTE event structure.
 */
static void lte_handler( const struct lte_lc_evt *const evt )
{
    switch( evt->type )
    {
        case LTE_LC_EVT_NW_REG_STATUS:

            if( ( evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME ) &&
                ( evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING ) )
            {
                break;
            }

            LOG_INF( "Network registration status: %s",
                     evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home" : "Connected - roaming" );
            k_sem_give( &lte_connected_sem );
            break;

        case LTE_LC_EVT_PSM_UPDATE:
            LOG_INF( "PSM parameter update: TAU: %d s, Active time: %d s",
                     evt->psm_cfg.tau, evt->psm_cfg.active_time );
            break;

        case LTE_LC_EVT_EDRX_UPDATE:
            LOG_INF( "eDRX parameter update: eDRX: %.2f s, PTW: %.2f s",
                     ( double ) evt->edrx_cfg.edrx, ( double ) evt->edrx_cfg.ptw );
            break;

        case LTE_LC_EVT_RRC_UPDATE:
            LOG_INF( "RRC mode: %s",
                     evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle" );
            break;

        case LTE_LC_EVT_CELL_UPDATE:
            LOG_INF( "LTE cell changed: Cell ID: %d, Tracking area: %d",
                     evt->cell.id, evt->cell.tac );
            break;

        case LTE_LC_EVT_RAI_UPDATE:
            /* RAI notification is supported by modem firmware releases >= 2.0.2 */
            LOG_INF( "RAI configuration update: "
                     "Cell ID: %d, MCC: %d, MNC: %d, AS-RAI: %d, CP-RAI: %d",
                     evt->rai_cfg.cell_id,
                     evt->rai_cfg.mcc,
                     evt->rai_cfg.mnc,
                     evt->rai_cfg.as_rai,
                     evt->rai_cfg.cp_rai );
            break;

        default:
            break;
    }
}

/**
 * @brief Thread function handling outgoing UDP packets.
 */
void uplink_thread_fn( void * p1,
                       void * p2,
                       void * p3 )
{
    int err;
    const int MAX_RETRIES = 5;
    int retry_count = 0;
    struct addrinfo * res;
    struct addrinfo hints =
    {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };

    LOG_INF( "Uplink thread started..." );
connect_retry:
    err = zsock_getaddrinfo( CONFIG_UDP_SERVER_HOSTNAME, NULL, &hints, &res );

    if( err < 0 )
    {
        LOG_ERR( "Failed to resolve hostname '%s' with getaddrinfo(), errno: %d (%s)",
                 CONFIG_UDP_SERVER_HOSTNAME, errno, strerror( errno ) );
        err = -errno;
        goto wait_and_retry;
    }

    ( ( struct sockaddr_in * ) res->ai_addr )->sin_port = htons( CONFIG_UDP_SERVER_PORT );
    uplink_fd = zsock_socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

    if( uplink_fd < 0 )
    {
        LOG_ERR( "Failed to create UDP socket: %d", errno );
        zsock_freeaddrinfo( res );
        err = -errno;
        goto wait_and_retry;
    }

    err = zsock_connect( uplink_fd, ( struct sockaddr * ) res->ai_addr,
                         sizeof( struct sockaddr_in ) );
    zsock_freeaddrinfo( res );

    if( err < 0 )
    {
        LOG_ERR( "Uplink connect failed : %d", errno );
        zsock_close( uplink_fd );
        uplink_fd = -1;
        goto wait_and_retry;
    }

    LOG_INF( "Hostname %s, port number %d",
             CONFIG_UDP_SERVER_HOSTNAME,
             CONFIG_UDP_SERVER_PORT );
    retry_count = 0;

    while( 1 )
    {
        #if !defined( CONFIG_NCE_ENERGY_SAVER )
        char buffer[] = CONFIG_PAYLOAD;
        LOG_INF( "Payload (string): %s", buffer );
        #else
        char buffer[ CONFIG_PAYLOAD_DATA_SIZE ];

        Element2byte_gen_t battery_level = { .type = E_INTEGER, .value.i = 99, .template_length = 1 };
        Element2byte_gen_t signal_strength = { .type = E_INTEGER, .value.i = 84, .template_length = 1 };
        Element2byte_gen_t software_version = { .type = E_STRING, .value.s = "2.2.1", .template_length = 5 };
        err = os_energy_save( buffer, 1, 3, battery_level, signal_strength, software_version );

        if( err < 0 )
        {
            LOG_ERR( "Failed to save energy, %d", errno );
        }

        LOG_INF( "Transmitting UDP/IP payload of %d bytes to the server %s:%d",
                 CONFIG_PAYLOAD_DATA_SIZE + UDP_IP_HEADER_SIZE, CONFIG_UDP_SERVER_HOSTNAME, CONFIG_UDP_SERVER_PORT );
        LOG_HEXDUMP_INF( buffer, sizeof( buffer ), "Payload (binary):" );
        #endif /* if !defined( CONFIG_NCE_ENERGY_SAVER ) */
        err = zsock_send( uplink_fd, buffer, sizeof( buffer ) - 1, 0 );

        if( err < 0 )
        {
            LOG_ERR( "Send failed (errno: %d), reconnecting...", errno );
            zsock_close( uplink_fd );
            uplink_fd = -1;
            goto wait_and_retry;
        }
        else
        {
            LOG_INF( "UDP packet sent (%d bytes)", err );
            #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
            if( ledBlue.port )
            {
                gpio_pin_set_dt( &ledBlue, 0 );
            }

            if( ledGreen.port )
            {
                gpio_pin_set_dt( &ledGreen, 100 );
            }
            #endif
        }

        k_sleep( K_SECONDS( CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS ) );
    }

wait_and_retry:
    retry_count++;

    if( retry_count < MAX_RETRIES )
    {
        LOG_WRN( "Retrying uplink (%d/%d)...", retry_count, MAX_RETRIES );
        k_sleep( K_SECONDS( 5 ) );
        goto connect_retry;
    }
    else
    {
        LOG_ERR( " Max uplink retries reached. Stopping thread." );

        if( uplink_fd > 0 )
        {
            zsock_close( uplink_fd );
            uplink_fd = -1;
        }

        return;
    }
}

#if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER )

/**
 * @brief Thread function handling incoming messages.
 */
void downlink_thread_fn( void * p1,
                         void * p2,
                         void * p3 )
{
    char buffer[ 256 ];
    struct sockaddr_in my_addr =
    {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl( INADDR_ANY ),
        .sin_port        = htons( CONFIG_NCE_RECV_PORT )
    };
    struct sockaddr_in sender_addr;
    socklen_t sender_addr_len = sizeof( sender_addr );
    const int MAX_RETRIES = 5;
    int retry_count = 0;

    LOG_INF( "Downlink thread started..." );
retry_downlink:
    /* Create socket */
    downlink_fd = zsock_socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

    if( downlink_fd < 0 )
    {
        LOG_ERR( "Failed to create downlink socket, errno: %d", errno );
        goto wait_and_retry;
    }

    if( zsock_bind( downlink_fd, ( struct sockaddr * ) &my_addr, sizeof( struct sockaddr_in ) ) < 0 )
    {
        LOG_ERR( "Bind failed on port %d, errno: %d", CONFIG_NCE_RECV_PORT, errno );
        zsock_close( downlink_fd );
        downlink_fd = -1;
        goto wait_and_retry;
    }

    LOG_INF( "Listening on port: %d", CONFIG_NCE_RECV_PORT );
    struct timeval timeout =
    {
        .tv_sec  = 60,
        .tv_usec = 0
    };

    if( zsock_setsockopt( downlink_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) ) < 0 )
    {
        LOG_ERR( "Failed to set socket timeout, errno: %d", errno );
        zsock_close( downlink_fd );
        downlink_fd = -1;
        goto wait_and_retry;
    }

    retry_count = 0;

    while( 1 )
    {
        ssize_t received_bytes = zsock_recvfrom( downlink_fd, buffer, sizeof( buffer ) - 1, 0,
                                                 ( struct sockaddr * ) &sender_addr, &sender_addr_len );

        if( received_bytes < 0 )
        {
            if( ( errno == EAGAIN ) || ( errno == EWOULDBLOCK ) )
            {
                LOG_WRN( "No message received within timeout" );
                continue; /* keep listening */
            }
            else
            {
                LOG_ERR( "recvfrom() failed, errno: %d", errno );
                zsock_close( downlink_fd );
                downlink_fd = -1;
                goto wait_and_retry;
            }
        }

        buffer[ received_bytes ] = '\0';
        LOG_INF( "Received message: %s", buffer );
    }

wait_and_retry:
    retry_count++;

    if( retry_count < MAX_RETRIES )
    {
        LOG_WRN( "Retrying downlink socket init (%d/%d)...", retry_count, MAX_RETRIES );
        k_sleep( K_SECONDS( 5 ) );
        goto retry_downlink;
    }
    else
    {
        LOG_ERR( "Max downlink retries reached. Stopping thread." );

        if( downlink_fd > 0 )
        {
            zsock_close( downlink_fd );
            downlink_fd = -1;
        }

        return;
    }
}
#endif /* if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER ) */

/******************************************************************************
* Main Function
******************************************************************************/
int main( void )
{
    int err;

    #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    configureLeds();
    k_sleep( K_SECONDS( 10 ) );

    if( ledRed.port )
    {
        gpio_pin_set_dt( &ledRed, 100 );
    }
    #endif
    err = nrf_modem_lib_init();

    if( err )
    {
        LOG_ERR( "Failed to initialize modem library, error: %d", err );
        return err;
    }

    err = lte_lc_connect_async( lte_handler );

    if( err )
    {
        LOG_ERR( "Failed to connect to LTE network, error: %d", err );
        return err;
    }

    k_sem_take( &lte_connected_sem, K_FOREVER );
    #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    if( ledRed.port )
    {
        gpio_pin_set_dt( &ledRed, 0 );
    }

    if( ledBlue.port )
    {
        gpio_pin_set_dt( &ledBlue, 100 );
    }
    #endif
    LOG_INF( "1NCE UDP sample started" );
    #if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER )
    k_tid_t downlink_tid = k_thread_create( &downlink_thread, downlink_thread_stack,
                                            K_THREAD_STACK_SIZEOF( downlink_thread_stack ),
                                            downlink_thread_fn,
                                            NULL, NULL, NULL,
                                            THREAD_PRIORITY, 0, K_NO_WAIT );
    k_thread_name_set( downlink_tid, "downlink_thread" );
    #endif
    k_tid_t uplink_tid = k_thread_create( &uplink_thread, uplink_thread_stack,
                                          K_THREAD_STACK_SIZEOF( uplink_thread_stack ),
                                          uplink_thread_fn,
                                          NULL, NULL, NULL,
                                          THREAD_PRIORITY, 0, K_NO_WAIT );
    k_thread_name_set( uplink_tid, "uplink_thread" );
    /* Delay or wait for the threads to complete */
    k_sleep( K_SECONDS( 2 ) );
    return 0;
}
