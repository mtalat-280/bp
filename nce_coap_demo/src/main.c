/******************************************************************************
 * @file    main.c
 * @brief   1NCE CoAP Client Demo
 * @details Based on Nordic Semiconductor's CoAP sample. Modified by 1NCE Team
 *          to integrate with 1NCE CoAP endpoints, DTLS onboarding, LED feedback,
 *          and Energy Saver support. This file contains the implementation of a
 *          CoAP client designed for communicating with 1NCE endpoints using the
 *          CoAP protocol over UDP or DTLS. It supports sending compressed payloads
 *          via Energy Saver, device onboarding using pre-shared credentials, and
 *          optional downlink message reception via the Device Controller feature.
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
#include <stdio.h>
#include <string.h>

#if defined( CONFIG_POSIX_API )
    #include <zephyr/posix/arpa/inet.h>
    #include <zephyr/posix/netdb.h>
    #include <zephyr/posix/sys/socket.h>
    #include <zephyr/posix/poll.h>
#else
    #include <zephyr/net/socket.h>
#endif /* CONFIG_POSIX_API */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/random/random.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include "nce_iot_c_sdk.h"
#include <network_interface_zephyr.h>

LOG_MODULE_REGISTER( NCE_COAP_DEMO, CONFIG_COAP_CLIENT_SAMPLE_LOG_LEVEL );

#if defined( CONFIG_NCE_ENABLE_DTLS )
    #include <modem/modem_key_mgmt.h>
    #include <nrf_modem_at.h>
    #include <zephyr/net/tls_credentials.h>
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */
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

/** @brief Event masks for Zephyr NET management events. */
#define L4_EVENT_MASK            ( NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED )
#define CONN_LAYER_EVENT_MASK    ( NET_EVENT_CONN_IF_FATAL_ERROR )

#define THREAD_PRIORITY          5


/** @brief Kernel stack and threading configurations */
#define UPLINK_STACK_SIZE    4096
K_THREAD_STACK_DEFINE( uplink_thread_stack, UPLINK_STACK_SIZE );
struct k_thread uplink_thread;
static int uplink_fd = -1;
/** @brief Construct CoAP URI path with configurable query parameter. */
#define CONFIG_URI_PATH    "/?" CONFIG_COAP_URI_QUERY
/** @brief CoAP Client structures. */
struct coap_client coap_client = { 0 };


#if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER )
    #define DOWNLINK_STACK_SIZE    3072
K_THREAD_STACK_DEFINE( downlink_thread_stack, DOWNLINK_STACK_SIZE );
struct k_thread downlink_thread;
static int downlink_fd = -1;
    #define COAP_CODE_CLASS_SIZE       32
    #define COAP_SUCCESS_CODE_CLASS    2
#endif
/** @brief Macro for handling fatal errors by rebooting the device. */
#define FATAL_ERROR()                                    \
        LOG_ERR( "Fatal error! Rebooting the device." ); \
        LOG_PANIC();                                     \
        IF_ENABLED( CONFIG_REBOOT, ( sys_reboot( 0 ) ) )

/** @brief Network management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;
static bool is_connected; /**< Indicates if network is connected. */

/** @brief Mutex and conditional variable for network connectivity signaling. */
K_MUTEX_DEFINE( network_connected_lock );
K_CONDVAR_DEFINE( network_connected );

#if defined( CONFIG_NCE_ENABLE_DTLS )
/** @brief Security tag for DTLS. */
const sec_tag_t tls_sec_tag[] =
{
    CONFIG_NCE_DTLS_SECURITY_TAG,
};
DtlsKey_t nceKey = { 0 };
int connection_failure_count = 0;
extern void sys_arch_reboot( int type );
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

#if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )

/**
 * @brief Configures the LED GPIOs if the device is ready.
 */
void configureLeds()
{
    int ret = 0;

    if( ledRed.port && !device_is_ready( ledRed.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it\n",
                 ret, ledRed.port->name );
        ledRed.port = NULL;
    }

    if( ledRed.port )
    {
        ret = gpio_pin_configure_dt( &ledRed, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d\n",
                     ret, ledRed.port->name, ledRed.pin );
            ledRed.port = NULL;
        }
    }

    if( ledGreen.port && !device_is_ready( ledGreen.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it\n",
                 ret, ledGreen.port->name );
        ledGreen.port = NULL;
    }

    if( ledGreen.port )
    {
        ret = gpio_pin_configure_dt( &ledGreen, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d\n",
                     ret, ledGreen.port->name, ledGreen.pin );
            ledGreen.port = NULL;
        }
    }

    if( ledBlue.port && !device_is_ready( ledBlue.port ) )
    {
        LOG_ERR( "Error %d: LED device %s is not ready; ignoring it\n",
                 ret, ledBlue.port->name );
        ledBlue.port = NULL;
    }

    if( ledBlue.port )
    {
        ret = gpio_pin_configure_dt( &ledBlue, GPIO_OUTPUT );

        if( ret != 0 )
        {
            LOG_ERR( "Error %d: failed to configure LED device %s pin %d\n",
                     ret, ledBlue.port->name, ledBlue.pin );
            ledBlue.port = NULL;
        }
    }
}
#endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */
/** @brief Waits for network connectivity before proceeding. */
static void wait_for_network( void )
{
    k_mutex_lock( &network_connected_lock, K_FOREVER );

    if( !is_connected )
    {
        LOG_INF( "Waiting for network connectivity" );
        k_condvar_wait( &network_connected, &network_connected_lock, K_FOREVER );
    }

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
    k_mutex_unlock( &network_connected_lock );
}

static void response_cb( int16_t code,
                         size_t offset,
                         const uint8_t * payload,
                         size_t len,
                         bool last_block,
                         void * user_data )
{
    if( code >= 0 )
    {
        LOG_INF( "CoAP response: code: 0x%x", code );
    }
    else
    {
        LOG_INF( "Response received with error code: %d", code );
    }
}
#if defined( CONFIG_NCE_ENABLE_DTLS )
/* Store DTLS Credentials in the modem */
int store_credentials( void )
{
    int err;
    char psk_hex[ 100 ];
    int cred_len;

    /* Convert PSK to HEX */
    cred_len = bin2hex( nceKey.Psk, strlen( nceKey.Psk ), psk_hex, sizeof( psk_hex ) );

    if( cred_len == 0 )
    {
        LOG_ERR( "PSK is too large to convert (%d)", -EOVERFLOW );
        return -EOVERFLOW;
    }

    /* Store DTLS Credentials */
    err = modem_key_mgmt_write( CONFIG_NCE_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, psk_hex, sizeof( psk_hex ) );
    LOG_DBG( "psk status: %d\n", err );
    LOG_INF( "psk: %s\n", nceKey.Psk );

    err = modem_key_mgmt_write( CONFIG_NCE_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, nceKey.PskIdentity, sizeof( nceKey.PskIdentity ) );
    LOG_DBG( "psk_id status: %d\n", err );
    LOG_INF( "PskIdentity: %s\n", nceKey.PskIdentity );

    return err;
}

/**
 * @brief Onboard the device by managing DTLS credentials.
 *
 * @param[in] overwrite Whether to overwrite existing credentials, should be set to true when DTLS connecting is failing.
 * @return 0 on success, negative error code on failure.
 */
static int prv_onboard_device( bool overwrite )
{
    /* If the configured tag contains DTLS credentials, the onboarding process is skipped */
    /* Unless "overwrite" is true,  */
    int err;
    bool exists;

    err = modem_key_mgmt_exists( CONFIG_NCE_DTLS_SECURITY_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, &exists );

    if( overwrite || !exists )
    {
        struct OSNetwork OSNetwork = { .os_socket = 0 };
        os_network_ops_t osNetwork =
        {
            .os_socket             = &OSNetwork,
            .nce_os_udp_connect    = nce_os_connect,
            .nce_os_udp_send       = nce_os_send,
            .nce_os_udp_recv       = nce_os_recv,
            .nce_os_udp_disconnect = nce_os_disconnect
        };
        err = os_auth( &osNetwork, &nceKey );

        if( err )
        {
            LOG_ERR( "1NCE SDK onboarding failed, err %d\n", errno );
            return err;
        }

        LOG_INF( "Disconnecting from the network to store credentials\n" );

        err = lte_lc_offline();

        if( err )
        {
            LOG_ERR( "Failed to disconnect from the LTE network, err %d\n", err );
            return err;
        }

        err = store_credentials();

        if( err )
        {
            LOG_ERR( "Failed to store credentials, err %d\n", errno );
            return err;
        }

        LOG_INF( "Rebooting to ensure changes take effect after saving credentials.." );

        sys_arch_reboot( 0 );
    }
    else
    {
        LOG_INF( "Device is already onboarded \n" );
    }

    return err;
}

/* Configure DTLS socket */
static int dtls_setup( int fd )
{
    int err;
    int verify;
    int role;

    /* Set up DTLS peer verification */
    enum
    {
        NONE = 0,
        OPTIONAL = 1,
        REQUIRED = 2,
    };

    verify = NONE;

    err = zsock_setsockopt( fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof( verify ) );

    if( err )
    {
        LOG_ERR( "[ERR]Failed to setup peer verification, err %d\n", errno );
        return err;
    }

    /* Set up DTLS client mode */
    enum
    {
        CLIENT = 0,
        SERVER = 1,
    };

    role = CLIENT;

    err = zsock_setsockopt( fd, SOL_TLS, TLS_DTLS_ROLE, &role, sizeof( role ) );

    if( err )
    {
        LOG_ERR( "[ERR]Failed to setup DTLS role, err %d\n", errno );
        return err;
    }

    /* Set up DTLS timeout */
    int dtls_timeo = CONFIG_NCE_DTLS_HANDSHAKE_TIMEOUT_SECONDS;

    err = setsockopt( fd, SOL_TLS, TLS_DTLS_HANDSHAKE_TIMEO, &dtls_timeo, sizeof( dtls_timeo ) );

    if( err )
    {
        LOG_WRN( "[WRN] Failed to setup DTLS HandTimout, err %d\n", errno );
    }

    /* Associate the socket with the security tag */
    err = zsock_setsockopt( fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
                            sizeof( tls_sec_tag ) );

    if( err )
    {
        LOG_ERR( "[ERR]Failed to setup TLS sec tag, err %d\n", errno );
        return err;
    }

    return 0;
}
/* Handles DTLS failure by onboarding the device with overwriting enabled  */
static int prv_handle_dtls_failure( void )
{
    int err;

    /* Onboard the device with overwrtiting enabled */
    err = prv_onboard_device( true );

    if( err )
    {
        LOG_ERR( "Device onboarding failed, err %d\n", err );
    }

    return err;
}
#endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

/** @brief Starts the uplink item. */
void uplink_thread_fn( void * p1,
                       void * p2,
                       void * p3 )
{
    int err;
    int retry_count = 0;
    const int MAX_RETRIES = CONFIG_NCE_UPLINK_MAX_RETRIES;
    struct addrinfo * resolved_info = NULL;
    struct coap_client_request req =
    {
        .method      = COAP_METHOD_POST,
        .confirmable = true,
        .fmt         = COAP_CONTENT_FORMAT_TEXT_PLAIN,
        .cb          = response_cb,
        .path        = CONFIG_URI_PATH,
    };

    LOG_INF( "Uplink thread started..." );

connect_retry:
    retry_count++;

    if( retry_count > MAX_RETRIES )
    {
        LOG_ERR( "Max uplink retries reached. Stopping uplink thread." );

        if( uplink_fd > 0 )
        {
            zsock_close( uplink_fd );
            uplink_fd = -1;
        }

        return;
    }

    /* DNS Resolution */
    {
        struct addrinfo dns_hints =
        {
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_DGRAM,
        };
        err = zsock_getaddrinfo( CONFIG_COAP_SAMPLE_SERVER_HOSTNAME, NULL, &dns_hints, &resolved_info );

        if( ( err < 0 ) || !resolved_info || !resolved_info->ai_addr )
        {
            LOG_ERR( "Failed to resolve hostname '%s', errno: %d", CONFIG_COAP_SAMPLE_SERVER_HOSTNAME, errno );
            err = -errno;
            goto wait_and_retry;
        }

        ( ( struct sockaddr_in * ) resolved_info->ai_addr )->sin_port = htons( CONFIG_COAP_SAMPLE_SERVER_PORT );
        LOG_INF( "DNS Resolution successful" );
    }
    #if defined( CONFIG_NCE_ENABLE_DTLS )
    uplink_fd = zsock_socket( AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2 );
    #else
    uplink_fd = zsock_socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

    if( uplink_fd < 0 )
    {
        LOG_ERR( "Failed to create CoAP Uplink socket: %d.", -errno );
        zsock_freeaddrinfo( resolved_info );
        goto wait_and_retry;
    }

    #if defined( CONFIG_NCE_ENABLE_DTLS )
    err = dtls_setup( uplink_fd );

    if( err )
    {
        LOG_ERR( "DTLS setup failed, err %d\n", err );
        goto close_and_retry;
    }
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */
    err = zsock_connect( uplink_fd, ( struct sockaddr * ) resolved_info->ai_addr, sizeof( struct sockaddr_in ) );
    zsock_freeaddrinfo( resolved_info );

    if( err )
    {
        #if defined( CONFIG_NCE_ENABLE_DTLS )
        connection_failure_count++;
        LOG_ERR( "Failed to Connect to Uplink CoAPs Server. (Attempt:%d)\n", connection_failure_count );
        #else
        LOG_ERR( "Failed to Connect Uplink to CoAP Server" );
        #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

        goto close_and_retry;
    }

    LOG_INF( "Connected to Uplink CoAP server %s:%d", CONFIG_COAP_SAMPLE_SERVER_HOSTNAME, CONFIG_COAP_SAMPLE_SERVER_PORT );

    while( 1 )
    {
        #if defined( CONFIG_NCE_ENERGY_SAVER )
        int converted_bytes = 0;
        char buffer[ CONFIG_NCE_PAYLOAD_DATA_SIZE ];

        LOG_INF( "\nCoAP client POST (Binary Payload)\n" );

        Element2byte_gen_t battery_level =
        {
            .type            = E_INTEGER,
            .value.i         = 99,
            .template_length = 1
        };
        Element2byte_gen_t signal_strength =
        {
            .type            = E_INTEGER,
            .value.i         = 84,
            .template_length = 1
        };
        Element2byte_gen_t software_version =
        {
            .type            = E_STRING,
            .value.s         = "2.2.1",
            .template_length = 5
        };

        converted_bytes = os_energy_save( buffer, 1, 3,
                                          battery_level,
                                          signal_strength,
                                          software_version );

        if( converted_bytes < 0 )
        {
            LOG_ERR( "Failed to save energy, %d", errno );
            goto close_and_retry;
        }

        req.payload = buffer;
        req.len = converted_bytes;
        LOG_HEXDUMP_INF( buffer, sizeof( buffer ), "Payload (binary):" );
        #else /* if defined( CONFIG_NCE_ENERGY_SAVER ) */
        req.payload = CONFIG_PAYLOAD;
        req.len = strlen( CONFIG_PAYLOAD );
        LOG_INF( "Payload: %s", CONFIG_PAYLOAD );
        #endif /* if defined( CONFIG_NCE_ENERGY_SAVER ) */
        /* Send request */
        err = coap_client_req( &coap_client, uplink_fd, NULL, &req, NULL );

        if( err )
        {
            LOG_ERR( "Failed to send request : %d", err );
            goto close_and_retry;
        }

        LOG_INF( "CoAP POST request sent to %s, resource: %s",
                 CONFIG_COAP_SAMPLE_SERVER_HOSTNAME, req.path );

        #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
        if( ledBlue.port )
        {
            gpio_pin_set_dt( &ledBlue, 0 );
        }

        if( ledGreen.port )
        {
            gpio_pin_set_dt( &ledGreen, 100 ); /* turn on green LED even if not acknowledged (NON CON) */
        }
        #endif
        k_sleep( K_SECONDS( CONFIG_COAP_SAMPLE_REQUEST_INTERVAL_SECONDS ) );
    }

close_and_retry:

    if( uplink_fd > 0 )
    {
        zsock_close( uplink_fd );
        uplink_fd = -1;
    }

    #if defined( CONFIG_NCE_ENABLE_DTLS )
    if( connection_failure_count >= CONFIG_NCE_MAX_DTLS_CONNECTION_ATTEMPTS )
    {
        LOG_WRN( "Max DTLS retries reached. Updating credentials..." );
        connection_failure_count = 0;
        err = prv_handle_dtls_failure();

        if( err )
        {
            LOG_ERR( "Handle dtls failure, err=%d", err );
            goto wait_and_retry;
        }
    }
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */

wait_and_retry:

    LOG_WRN( "Retrying uplink (%d/%d)...", retry_count, MAX_RETRIES );
    k_sleep( K_SECONDS( 5 ) );
    goto connect_retry;
}


#if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER )
/** @brief Initialize and send a CoAP acknowledgment. */
static int send_coap_ack( int sock,
                          struct coap_packet * packet,
                          struct sockaddr * addr,
                          socklen_t addr_len )
{
    int err;
    struct coap_packet ack;
    uint8_t * data;

    data = ( uint8_t * ) k_malloc( MAX_COAP_MSG_LEN );

    if( !data )
    {
        return -ENOMEM;
    }

    err = coap_ack_init( &ack, packet, data, MAX_COAP_MSG_LEN, COAP_RESPONSE_CODE_CHANGED );

    if( err < 0 )
    {
        LOG_ERR( "Failed to init CoAP ACK \n" );
        goto end;
    }

    LOG_HEXDUMP_INF( ack.data, ack.offset, "sent ack:" );
    err = zsock_sendto( sock, ack.data, ack.offset, 0, addr, addr_len );

    if( err < 0 )
    {
        LOG_ERR( "Failed to init CoAP ACK (msg ID: %u, errno: %d)", coap_header_get_id( packet ), errno );
        goto end;
    }

end:
    k_free( data );
    return err;
}
/** @brief Print CoAP message details. */
void print_coap_options( struct coap_packet * packet )
{
    LOG_INF( "CoAP Options:" );
    /* Create a coap_option array to hold the found options */
    struct coap_option option_data[ CONFIG_NCE_COAP_MAX_URI_PATH_SEGMENTS ];
    int num_options = coap_find_options( packet, COAP_OPTION_URI_PATH, option_data, CONFIG_NCE_COAP_MAX_URI_PATH_SEGMENTS );

    /* If the Uri-Path option exists, extract and print the path */
    if( num_options > 0 )
    {
        LOG_INF( "Complete Path: " );

        for(int i = 0; i < num_options; i++)
        {
            struct coap_option * uri_path_opt = &option_data[ i ];
            LOG_INF( "/%.*s\n", uri_path_opt->len, uri_path_opt->value );
        }
    }

    /* Create a coap_option array to hold the found Uri-Query options */
    struct coap_option options_query[ CONFIG_NCE_COAP_MAX_URI_QUERY_PARAMS ];

    /* Find the Uri-Query options (option number 15) */
    int num_query_options = coap_find_options( packet, COAP_OPTION_URI_QUERY, options_query, CONFIG_NCE_COAP_MAX_URI_QUERY_PARAMS );

    /* If the Uri-Query options exist, extract and print the query */
    if( num_query_options > 0 )
    {
        LOG_INF( "Query: " );

        for(int i = 0; i < num_query_options; i++)
        {
            struct coap_option * uri_query_opt = &options_query[ i ];
            LOG_INF( "%.*s", uri_query_opt->len, uri_query_opt->value );

            if( i < num_query_options - 1 )
            {
                LOG_INF( "&" );
            }
        }
    }
}
static const char *coap_method_to_string( uint8_t code )
{
    switch( code )
    {
        case 1:
            return "GET";

        case 2:
            return "POST";

        case 3:
            return "PUT";

        case 4:
            return "DELETE";

        case 5:
            return "PATCH"; /* RFC 8132 */

        case 6:
            return "iPATCH"; /* RFC 8132 */

        default:
            return "UNKNOWN";
    }
}

void check_and_print_coap_response_code( struct coap_packet * packet )
{
    uint8_t code = coap_header_get_code( packet );
    uint8_t code_class = code / COAP_CODE_CLASS_SIZE;
    uint8_t code_detail = code % COAP_CODE_CLASS_SIZE;

    if( code_class == 0 )
    {
        const char * method_str = coap_method_to_string( code );
        LOG_INF( "CoAP Request Method: %s (%d.%02d)", method_str, code_class, code_detail );
    }
    else
    {
        LOG_WRN( "Not a request (code class = %d.%02d)", code_class, code_detail );
    }
}
void print_coap_payload( struct coap_packet * packet )
{
    uint16_t payload_len;

    if( !packet )
    {
        LOG_ERR( "Null packet passed to CoAP print function" );
        return;
    }

    const uint8_t * payload = coap_packet_get_payload( packet, &payload_len );

    if( !payload )
    {
        LOG_WRN( "No Payload to be printed" );
        return;
    }

    bool printable = true;

    for(int i = 0; i < payload_len; i++)
    {
        if( ( payload[ i ] < 32 ) || ( payload[ i ] > 126 ) )
        {
            printable = false;
            break;
        }
    }

    if( printable )
    {
        LOG_INF( "CoAP Payload: %.*s\n", payload_len, payload );
    }
    else
    {
        LOG_HEXDUMP_INF( payload, payload_len, "CoAP Payload (binary):" );
    }
}
void print_coap_header( struct coap_packet * packet )
{
    if( !packet )
    {
        LOG_ERR( "Null packet passed to CoAP print function" );
        return;
    }

    LOG_INF( "CoAP Header:" );
    LOG_INF( "Version: %d", coap_header_get_version( packet ) );
    LOG_INF( "Type: %s", coap_header_get_type( packet ) == COAP_TYPE_CON ? "CON" : "NON" );
    check_and_print_coap_response_code( packet );
    LOG_INF( "Message ID: %u", coap_header_get_id( packet ) );
}
void print_coap_message( struct coap_packet * packet )
{
    if( !packet )
    {
        LOG_ERR( "Null packet passed to CoAP print function" );
        return;
    }

    print_coap_header( packet );
    print_coap_options( packet );
    print_coap_payload( packet );
}
/** @brief Downlink function: Listens for incoming CoAP messages */
void downlink_thread_fn( void * p1,
                         void * p2,
                         void * p3 )
{
    int err;
    const int MAX_RETRIES = CONFIG_NCE_DOWNLINK_MAX_RETRIES;
    int retry_count = 0;
    struct coap_packet response;
    char buffer[ CONFIG_NCE_RECEIVE_BUFFER_SIZE ];
    /* struct coap_packet response; */
    struct sockaddr_in my_addr =
    {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl( INADDR_ANY ),
        .sin_port        = htons( CONFIG_NCE_RECV_PORT )
    };
    struct sockaddr sender_addr;
    socklen_t sender_addr_len = sizeof( sender_addr );

    LOG_INF( "Downlink thread started..." );
retry_downlink:

    downlink_fd = zsock_socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

    if( downlink_fd < 0 )
    {
        LOG_ERR( "Failed to create downlink socket, errno: %d", errno );
        goto wait_and_retry;
    }

    /* Bind to the set port and IP */
    if( zsock_bind( downlink_fd, ( struct sockaddr * ) &my_addr, sizeof( struct sockaddr_in ) ) < 0 )
    {
        LOG_ERR( "Bind failed on port %d, errno: %d", CONFIG_NCE_RECV_PORT, errno );
        zsock_close( downlink_fd );
        downlink_fd = -1;
        goto wait_and_retry;
    }

    LOG_INF( "Listening on port: %d\n", CONFIG_NCE_RECV_PORT );

    /* Set the timeout value */
    struct timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;

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
                k_msleep( 100 );
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
        LOG_INF( "Received %d bytes from server", received_bytes );
        LOG_HEXDUMP_INF( buffer, received_bytes, "Received raw data:" );

        /* Parse CoAP message */
        err = coap_packet_parse( &response, buffer, received_bytes, NULL, 0 );

        if( err < 0 )
        {
            LOG_ERR( "coap_packet_parse() failed: %d", err );
            continue;
        }

        print_coap_message( &response );

        /* Reply with CoAP ACK */
        err = send_coap_ack( downlink_fd, &response, &sender_addr, sender_addr_len );

        if( err < 0 )
        {
            LOG_ERR( "send_coap_ack() failed: %d\n", err );
        }
        else
        {
            LOG_INF( "CoAP ACK sent successfully" );
        }
    }

wait_and_retry:
    retry_count++;

    if( retry_count < MAX_RETRIES )
    {
        LOG_WRN( "Retrying downlink socket setup (%d/%d)...", retry_count, MAX_RETRIES );
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

static void l4_event_handler( struct net_mgmt_event_callback * cb,
                              uint32_t event,
                              struct net_if * iface )
{
    switch( event )
    {
        case NET_EVENT_L4_CONNECTED:
            LOG_INF( "Network connectivity established" );
            k_mutex_lock( &network_connected_lock, K_FOREVER );
            is_connected = true;
            k_condvar_signal( &network_connected );
            k_mutex_unlock( &network_connected_lock );
            break;

        case NET_EVENT_L4_DISCONNECTED:
            LOG_INF( "Network connectivity lost" );
            k_mutex_lock( &network_connected_lock, K_FOREVER );
            is_connected = false;
            k_mutex_unlock( &network_connected_lock );
            break;

        default:
            /* Don't care */
            return;
    }
}
static void connectivity_event_handler( struct net_mgmt_event_callback * cb,
                                        uint32_t event,
                                        struct net_if * iface )
{
    if( event == NET_EVENT_CONN_IF_FATAL_ERROR )
    {
        LOG_ERR( "NET_EVENT_CONN_IF_FATAL_ERROR" );
        FATAL_ERROR();
        return;
    }
}
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
    /* Setup handler for Zephyr NET Connection Manager events and Connectivity layer. */
    net_mgmt_init_event_callback( &l4_cb, l4_event_handler, L4_EVENT_MASK );
    net_mgmt_add_event_callback( &l4_cb );

    net_mgmt_init_event_callback( &conn_cb, connectivity_event_handler, CONN_LAYER_EVENT_MASK );
    net_mgmt_add_event_callback( &conn_cb );

    /* Bring all network interfaces up.
     * Wi-Fi or LTE depending on the board that the sample was built for.
     */
    LOG_INF( "Bringing network interface up and connecting to the network" );

    err = conn_mgr_all_if_up( true );

    if( err )
    {
        LOG_ERR( "conn_mgr_all_if_up, error: %d", err );
        FATAL_ERROR();
        return err;
    }

    err = conn_mgr_all_if_connect( true );

    if( err )
    {
        LOG_ERR( "conn_mgr_all_if_connect, error: %d", err );
        FATAL_ERROR();
        return err;
    }

    /* Resend connection status if the sample is built for NATIVE_SIM.
     * This is necessary because the network interface is automatically brought up
     * at SYS_INIT() before main() is called.
     * This means that NET_EVENT_L4_CONNECTED fires before the
     * appropriate handler l4_event_handler() is registered.
     */
    if( IS_ENABLED( CONFIG_BOARD_NATIVE_SIM ) )
    {
        conn_mgr_mon_resend_status();
    }

    wait_for_network();

    #if defined( CONFIG_NCE_ENABLE_DTLS )
    /* Check for existing PSK on device */
    err = prv_onboard_device( false );

    if( err )
    {
        LOG_ERR( "Device onboarding failed, err %d\n", err );
        return err;
    }
    LOG_INF( "Device onboarded successfully \n" );
    #endif /* if defined( CONFIG_NCE_ENABLE_DTLS ) */
    LOG_INF( "1NCE CoAP Demo started" );
    LOG_INF( "Initializing CoAP client on port: %d", CONFIG_COAP_SAMPLE_SERVER_PORT );
    err = coap_client_init( &coap_client, NULL );

    if( err )
    {
        LOG_ERR( "Failed to initialize CoAP client: %d", err );
        return err;
    }

    k_tid_t uplink_tid = k_thread_create( &uplink_thread, uplink_thread_stack,
                                          K_THREAD_STACK_SIZEOF( uplink_thread_stack ),
                                          uplink_thread_fn,
                                          NULL, NULL, NULL,
                                          THREAD_PRIORITY, 0, K_NO_WAIT );
    k_thread_name_set( uplink_tid, "uplink_thread" );

    #if defined( CONFIG_NCE_ENABLE_DEVICE_CONTROLLER )
    k_tid_t downlink_tid = k_thread_create( &downlink_thread, downlink_thread_stack,
                                            K_THREAD_STACK_SIZEOF( downlink_thread_stack ),
                                            downlink_thread_fn,
                                            NULL, NULL, NULL,
                                            THREAD_PRIORITY, 0, K_NO_WAIT );
    k_thread_name_set( downlink_tid, "downlink_thread" );
    #endif

    /* Delay or wait for the threads to complete */
    k_sleep( K_SECONDS( 2 ) );

    return 0;
}
