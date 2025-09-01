/******************************************************************************
 * @file       main.c
 * @brief      LwM2M Client Application
 * @details    This file is based on Nordic Semiconductor's LwM2M client sample.
 *             It includes custom extensions for device onboarding, LED handling,
 *             and support for 1NCE connectivity and credentials management.
 *
 * @copyright  Portions copyright (c) 2019 Nordic Semiconductor ASA
 *             Modified by 1NCE GmbH (c) 2024
 ******************************************************************************/

// SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

/******************************************************************************
 * Includes
 ******************************************************************************/
#include <zephyr/kernel.h>
#include <ctype.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>
#include <zephyr/net/lwm2m.h>
#include <modem/nrf_modem_lib.h>
#include <net/lwm2m_client_utils.h>
#include <app_event_manager.h>
#include <net/lwm2m_client_utils_location.h>
#include <date_time.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER( app_lwm2m_client, CONFIG_APP_LOG_LEVEL );

#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>

#include "lwm2m_client_app.h"
#include "lwm2m_app_utils.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <modem/modem_key_mgmt.h>

#if defined( CONFIG_LWM2M_CLIENT_UTILS_LOCATION_ASSISTANCE )
    #include "ui_input.h"
    #include "ui_input_event.h"
#endif

#if !defined( CONFIG_LTE_LINK_CONTROL )
    #error "Missing CONFIG_LTE_LINK_CONTROL"
#endif

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

#define APP_BANNER                       "Run LWM2M client"


#define LWM2M_SECURITY_PRE_SHARED_KEY    0
#define LWM2M_SECURITY_RAW_PUBLIC_KEY    1
#define LWM2M_SECURITY_CERTIFICATE       2
#define LWM2M_SECURITY_NO_SEC            3

#define CONNEVAL_MAX_DELAY_S             60
#define CONNEVAL_POLL_PERIOD_MS          5000

/* Client State Machine states */
static enum client_state
{
    START,        /* Start Connection to a server*/
    CONNECTING,   /* LwM2M engine is connecting to server */
    BOOTSTRAP,    /* LwM2M engine is doing a bootstrap */
    CONNECTED,    /* LwM2M Client connection establisment to server */
    LTE_OFFLINE,  /* LTE offline and LwM2M engine should be suspended */
    NETWORK_ERROR /* Client network error handling. Client stop and modem reset */
}
client_state = START;

static uint8_t endpoint_name[] = CONFIG_NCE_ICCID;
/* static uint8_t imei_buf[IMEI_LEN + sizeof("\r\nOK\r\n")]; */
static struct lwm2m_ctx client = { 0 };
static bool reconnect;
static K_SEM_DEFINE( state_mutex, 0, 1 );
static K_MUTEX_DEFINE( lte_mutex );
static bool modem_connected_to_network;
/* Enable session lifetime check for initial boot */
static bool update_session_lifetime = true;

static void rd_client_event( struct lwm2m_ctx * client,
                             enum lwm2m_rd_client_event client_event );
static void state_trigger_and_unlock( enum client_state new_state );

void client_acknowledge( void )
{
    lwm2m_acknowledge( &client );
}

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

#if defined( CONFIG_LWM2M_CLIENT_UTILS_SIGNAL_MEAS_INFO_OBJ_SUPPORT )
static struct k_work_delayable ncell_meas_work;
void ncell_meas_work_handler( struct k_work * work )
{
    lwm2m_ncell_schedule_measurement();
    k_work_schedule( &ncell_meas_work, K_SECONDS( CONFIG_APP_NEIGHBOUR_CELL_SCAN_INTERVAL ) );
}
#endif
#if defined( CONFIG_LWM2M_CLIENT_UTILS_VISIBLE_WIFI_AP_OBJ_SUPPORT )
static struct k_work_delayable ground_fix_work;
void ground_fix_work_handler( struct k_work * work )
{
    LOG_INF( "Send ground fix location request event" );
    struct ground_fix_location_request_event * ground_fix_event =
        new_ground_fix_location_request_event();

    APP_EVENT_SUBMIT( ground_fix_event );
}
#endif

#if defined( CONFIG_APP_LWM2M_CONFORMANCE_TESTING )
static struct k_work_delayable send_periodical_work;
static uint8_t send_count = 0;

static int server_send_mute_cb( uint16_t obj_inst_id,
                                uint16_t res_id,
                                uint16_t res_inst_id,
                                uint8_t * data,
                                uint16_t data_len,
                                bool last_block,
                                size_t total_size,
                                size_t offset )
{
    if( *data )
    {
        LOG_INF( "Server Muted Send" );
    }
    else
    {
        if( send_count == 0 )
        {
            LOG_INF( "Server Activate Send" );
            send_count = 5;
            k_work_schedule( &send_periodical_work, K_SECONDS( 1 ) );
        }
    }

    return 0;
}

static void lwm2m_register_server_send_mute_cb( void )
{
    int ret;
    lwm2m_engine_set_data_cb_t cb;

    cb = server_send_mute_cb;
    ret = lwm2m_register_post_write_callback( &LWM2M_OBJ( 1, client.srv_obj_inst, 23 ), cb );

    if( ret )
    {
        LOG_ERR( "Send enable CB fail %d", ret );
    }
}

void send_periodically_work_handler( struct k_work * work )
{
    int ret;
    const struct lwm2m_obj_path send_path[ 4 ] =
    {
        LWM2M_OBJ( 3, 0, 0 ),
        LWM2M_OBJ( 3, 0, 3 ),
        LWM2M_OBJ( 3, 0, 13 ),
        LWM2M_OBJ( 3, 0, 19 ),
    };

    /* lwm2m send post to server */
    ret = lwm2m_send_cb( &client, send_path, 4, NULL );

    if( ret )
    {
        if( ret == EPERM )
        {
            LOG_INF( "Server Mute send block send operation" );
        }
        else
        {
            LOG_INF( "Periodically SEND test data fail %d", ret );
        }
    }

    if( send_count )
    {
        if( ret == 0 )
        {
            send_count--;
        }

        k_work_schedule( &send_periodical_work, K_SECONDS( 15 ) );
    }
}
#endif /* if defined( CONFIG_APP_LWM2M_CONFORMANCE_TESTING ) */

static void state_trigger_and_unlock( enum client_state new_state )
{
    if( new_state != client_state )
    {
        client_state = new_state;
        k_sem_give( &state_mutex );
    }

    k_mutex_unlock( &lte_mutex );
}

static int lwm2m_setup( void )
{
    /* Save power by not updating timestamp on device object */
    lwm2m_update_device_service_period( 0 );

    /* Manufacturer dependent */
    /* use IMEI as serial number */
    lwm2m_app_init_device( endpoint_name );
    lwm2m_init_security( &client, endpoint_name, NULL );

    if( false && sizeof( CONFIG_NCE_LWM2M_BOOTSTRAP_PSK ) > 1 )
    {
        /* Write hard-coded PSK key to engine */
        /* First security instance is the right one, because in bootstrap mode, */
        /* it is the bootstrap PSK. In normal mode, it is the server key */
        lwm2m_security_set_psk( 0, CONFIG_NCE_LWM2M_BOOTSTRAP_PSK, sizeof( CONFIG_NCE_LWM2M_BOOTSTRAP_PSK ), true,
                                endpoint_name );
    }

    #if defined( CONFIG_LWM2M_CLIENT_UTILS_LOCATION_ASSISTANCE )
    location_event_handler_init( &client );
    location_assistance_retry_init( true );
    #endif

    if( IS_ENABLED( CONFIG_LTE_LC_TAU_PRE_WARNING_NOTIFICATIONS ) ||
        IS_ENABLED( CONFIG_LWM2M_CLIENT_UTILS_NEIGHBOUR_CELL_LISTENER ) )
    {
        lwm2m_ncell_handler_register();
    }

    return 0;
}

static void date_time_event_handler( const struct date_time_evt * evt )
{
    switch( evt->type )
    {
        case DATE_TIME_OBTAINED_MODEM:
           {
               int64_t time = 0;

               LOG_INF( "Obtained date-time from modem" );
               date_time_now( &time );
               lwm2m_set_s32( &LWM2M_OBJ( LWM2M_OBJECT_DEVICE_ID, 0, CURRENT_TIME_RID ),
                              ( int32_t ) ( time / 1000 ) );
               break;
           }

        case DATE_TIME_OBTAINED_NTP:
           {
               int64_t time = 0;

               LOG_INF( "Obtained date-time from NTP server" );
               date_time_now( &time );
               lwm2m_set_s32( &LWM2M_OBJ( LWM2M_OBJECT_DEVICE_ID, 0, CURRENT_TIME_RID ),
                              ( int32_t ) ( time / 1000 ) );
               break;
           }

        case DATE_TIME_NOT_OBTAINED:
            LOG_INF( "Could not obtain date-time update" );
            break;

        default:
            break;
    }
}

int store_credentials( void )
{
    int err;
    bool exists = false;

    /* Store Bootstrapping Credentials */
    err = modem_key_mgmt_write( CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, CONFIG_NCE_LWM2M_BOOTSTRAP_PSK, sizeof( CONFIG_NCE_LWM2M_BOOTSTRAP_PSK ) );
    LOG_DBG( "Bootstrap DTLS PSK storage status: %d\n", err );

    err = modem_key_mgmt_write( CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, CONFIG_NCE_ICCID, sizeof( CONFIG_NCE_ICCID ) );
    LOG_DBG( "Bootstrap DTLS Identity storage status: %d\n", err );

    /* Free LwM2M server Credentials if they exists*/
    err = modem_key_mgmt_exists( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, &exists );
    LOG_DBG( "LwM2M Server DTLS PSK exist status: %d\n", err );
    err = modem_key_mgmt_exists( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, &exists );
    LOG_DBG( "LwM2M Server DTLS Identity exist status: %d\n", err );

    if( ( err == 0 ) && exists )
    {
        err = modem_key_mgmt_delete( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK );
        LOG_DBG( "LwM2M Server DTLS PSK Deletion status: %d\n", err );
        err = modem_key_mgmt_delete( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY );
        LOG_DBG( "LwM2M Server DTLS Identity Deletion status: %d\n", err );
    }
    else
    {
        LOG_DBG( "No Credentials found\n" );
    }

    return err;
}

int reset_credentials( void )
{
    int err;
    bool exists = false;

    /* Free LwM2M server Credentials if they exists*/
    err = modem_key_mgmt_exists( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK, &exists );
    LOG_DBG( "LwM2M Server DTLS PSK exist status: %d\n", err );
    err = modem_key_mgmt_exists( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY, &exists );
    LOG_DBG( "LwM2M Server DTLS Identity exist status: %d\n", err );

    if( ( err == 0 ) && exists )
    {
        err = modem_key_mgmt_delete( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_PSK );
        LOG_DBG( "LwM2M Server DTLS PSK Deletion status: %d\n", err );
        err = modem_key_mgmt_delete( CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY );
        LOG_DBG( "LwM2M Server DTLS Identity Deletion status: %d\n", err );
    }
    else
    {
        LOG_DBG( "No Credentials found\n" );
    }

    return err;
}

static void rd_client_update_lifetime( int srv_obj_inst )
{
    uint32_t current_lifetime = 0;

    uint32_t lifetime = CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME;

    struct lwm2m_obj_path path = LWM2M_OBJ( 1, srv_obj_inst, 1 );

    lwm2m_get_u32( &path, &current_lifetime );

    if( current_lifetime != lifetime )
    {
        /* SET Configured value */
        lwm2m_set_u32( &path, lifetime );
        LOG_DBG( "Update session lifetime from %d to %d", current_lifetime, lifetime );
    }

    update_session_lifetime = false;
}

static void state_set_and_unlock( enum client_state new_state )
{
    client_state = new_state;
    k_mutex_unlock( &lte_mutex );
}

static void rd_client_event( struct lwm2m_ctx * client,
                             enum lwm2m_rd_client_event client_event )
{
    k_mutex_lock( &lte_mutex, K_FOREVER );

    if( ( client_state == LTE_OFFLINE ) && ( client_event != LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED ) )
    {
        LOG_DBG( "Drop network event %d at LTE offline state", client_event );
        k_mutex_unlock( &lte_mutex );
        return;
    }

    lwm2m_utils_connection_manage( client, &client_event );

    switch( client_event )
    {
        case LWM2M_RD_CLIENT_EVENT_SERVER_DISABLED:
        case LWM2M_RD_CLIENT_EVENT_DEREGISTER:
        case LWM2M_RD_CLIENT_EVENT_NONE:
            /* do nothing */
            k_mutex_unlock( &lte_mutex );
            break;

        case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
            LOG_DBG( "Bootstrap registration failure!" );
            state_trigger_and_unlock( NETWORK_ERROR );
            break;

        case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
            LOG_DBG( "Bootstrap registration complete" );
            update_session_lifetime = true;
            state_trigger_and_unlock( BOOTSTRAP );
            break;

        case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
            LOG_DBG( "Bootstrap transfer complete" );
            k_mutex_unlock( &lte_mutex );
            break;

        case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
            LOG_WRN( "Registration failure!" );
            state_trigger_and_unlock( CONNECTING );
            break;

        case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
            LOG_DBG( "Registration complete" );
            #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
            if( ledBlue.port )
            {
                gpio_pin_set_dt( &ledBlue, 0 );
            }

            if( ledGreen.port )
            {
                gpio_pin_set_dt( &ledGreen, 100 );
            }

            k_sleep( K_SECONDS( 10 ) );

            if( ledGreen.port )
            {
                gpio_pin_set_dt( &ledGreen, 0 );
            }
            #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */
            state_trigger_and_unlock( CONNECTED );
            break;

        case LWM2M_RD_CLIENT_EVENT_REG_TIMEOUT:
            LOG_DBG( "Registration update failure!" );
            state_trigger_and_unlock( CONNECTING );
            break;

        case LWM2M_RD_CLIENT_EVENT_REG_UPDATE:
            LOG_DBG( "Registration update started" );
            k_mutex_unlock( &lte_mutex );
            break;

        case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
            LOG_DBG( "Registration update complete" );
            state_trigger_and_unlock( CONNECTED );
            break;

        case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
            LOG_DBG( "Deregister failure!" );
            state_set_and_unlock( START );
            break;

        case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
            LOG_DBG( "Disconnected" );
            state_set_and_unlock( START );
            break;

        case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
            LOG_DBG( "Queue mode RX window closed" );
            k_mutex_unlock( &lte_mutex );
            break;

        case LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED:
            LOG_DBG( "LwM2M engine suspended" );
            k_mutex_unlock( &lte_mutex );
            break;

        case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
            LOG_ERR( "LwM2M engine reported a network error." );
            reconnect = true;
            state_trigger_and_unlock( NETWORK_ERROR );
            break;
    }
}

static void modem_connect( void )
{
    int ret;

    do
    {
        LOG_INF( "Connecting to network." );
        LOG_INF( "This may take several minutes." );

        ret = lte_lc_connect();

        if( ret < 0 )
        {
            LOG_WRN( "Failed to establish LTE connection (%d).", ret );
            LOG_WRN( "Will retry in a minute." );
            lte_lc_offline();
            k_sleep( K_SECONDS( 60 ) );
        }
        else
        {
            enum lte_lc_lte_mode mode;

            lte_lc_lte_mode_get( &mode );

            if( mode == LTE_LC_LTE_MODE_NBIOT )
            {
                LOG_INF( "Connected to NB-IoT network" );
            }
            else if( mode == LTE_LC_LTE_MODE_LTEM )
            {
                LOG_INF( "Connected to LTE network" );
            }
            else
            {
                LOG_INF( "Connected to unknown network" );
            }
        }
    } while( ret < 0 );

    if( IS_ENABLED( CONFIG_LWM2M_CLIENT_UTILS_LTE_CONNEVAL ) )
    {
        ret = lwm2m_utils_enable_conneval( LTE_LC_ENERGY_CONSUMPTION_NORMAL,
                                           CONNEVAL_MAX_DELAY_S, CONNEVAL_POLL_PERIOD_MS );

        if( ret < 0 )
        {
            LOG_ERR( "Failed to enable conneval (%d)", ret );
        }
        else
        {
            LOG_INF( "Conneval enabled" );
        }
    }
}

static bool lte_connected( enum lte_lc_nw_reg_status nw_reg_status )
{
    if( ( nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ) ||
        ( nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING ) )
    {
        return true;
    }

    return false;
}

static void lwm2m_lte_reg_handler_notify( enum lte_lc_nw_reg_status nw_reg_status )
{
    bool lte_registered;

    LOG_DBG( "LTE NW status: %d", nw_reg_status );
    k_mutex_lock( &lte_mutex, K_FOREVER );
    lte_registered = lte_connected( nw_reg_status );

    if( lte_registered != modem_connected_to_network )
    {
        modem_connected_to_network = lte_registered;

        if( ( client_state != START ) && ( client_state != BOOTSTRAP ) )
        {
            k_sem_give( &state_mutex );
        }
    }

    k_mutex_unlock( &lte_mutex );
}

#ifdef CONFIG_LTE_LC_MODEM_SLEEP_NOTIFICATIONS
static void lte_modem_enter_sleep( const struct lte_lc_modem_sleep * event )
{
    switch( event->type )
    {
        case LTE_LC_MODEM_SLEEP_PSM:
        case LTE_LC_MODEM_SLEEP_PROPRIETARY_PSM:
            LOG_INF( "Modem Enter PSM, time %lld", event->time );
            break;

        case LTE_LC_MODEM_SLEEP_RF_INACTIVITY:
            LOG_INF( "Modem Enter eDRX state, time %lld", event->time );
            break;

        default:
            break;
    }
}
#endif /* ifdef CONFIG_LTE_LC_MODEM_SLEEP_NOTIFICATIONS */

static void lte_notify_handler( const struct lte_lc_evt *const evt )
{
    switch( evt->type )
    {
        case LTE_LC_EVT_NW_REG_STATUS:
            lwm2m_lte_reg_handler_notify( evt->nw_reg_status );
            break;

            #ifdef CONFIG_LTE_LC_MODEM_SLEEP_NOTIFICATIONS
        case LTE_LC_EVT_MODEM_SLEEP_ENTER:
            lte_modem_enter_sleep( &evt->modem_sleep );
            break;
            #endif
        default:
            break;
    }
}

static void suspend_lwm2m_engine( void )
{
    int ret;

    state_trigger_and_unlock( LTE_OFFLINE );
    ret = lwm2m_engine_pause();

    if( ret )
    {
        LOG_ERR( "LwM2M engine pause fail %d", ret );
        reconnect = true;
        k_mutex_lock( &lte_mutex, K_FOREVER );
        state_trigger_and_unlock( NETWORK_ERROR );
    }
}

int main( void )
{
    int ret;
    uint32_t bootstrap_flags = 0;

    LOG_INF( APP_BANNER );

    #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
    configureLeds();
    k_sleep( K_SECONDS( 10 ) );

    if( ledRed.port )
    {
        gpio_pin_set_dt( &ledRed, 100 );
    }
    #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */

    ret = nrf_modem_lib_init();

    if( ret < 0 )
    {
        LOG_ERR( "Unable to init modem library (%d)", ret );
        return 0;
    }

    if( strlen( CONFIG_NCE_ICCID ) < 1 )
    {
        LOG_ERR( "[1NCE] Failed to read CONFIG_NCE_ICCID " );
        return 0;
    }

    ret = app_event_manager_init();

    if( ret )
    {
        LOG_ERR( "Unable to init Application Event Manager (%d)", ret );
        return 0;
    }

    lte_lc_register_handler( lte_notify_handler );

    ret = modem_info_init();

    if( ret < 0 )
    {
        LOG_ERR( "Unable to init modem_info (%d)", ret );
        return 0;
    }
    
    
    #if defined( CONFIG_LWM2M_DTLS_SUPPORT )
	LOG_INF( "Disconnecting from the network to store credentials\n" );
    ret = lte_lc_offline();

    if( ret )
    {
        LOG_ERR( "Failed to disconnect from the LTE network, err %d\n", ret );
        return ret;
    }

    ret = reset_credentials();

    if( ret )
    {
        LOG_ERR( "Failed to reset credentials, err %d\n", errno );
        return ret;
    }

    LOG_INF( "Reconnecting after storing credentials.. " );
    
    #endif /* ifdef CONFIG_LWM2M_DTLS_SUPPORT */

	ret = lte_lc_connect();

    if( ret )
    {
        LOG_ERR( "Failed to connect to the LTE network, err %d\n", ret );
        return ret;
    }

    LOG_INF( "endpoint: %s", ( char * ) endpoint_name );

    /* Setup LwM2M */
    ret = lwm2m_setup();

    if( ret < 0 )
    {
        LOG_ERR( "Failed to setup LWM2M fields (%d)", ret );
        return 0;
    }

    modem_connect();

    #if defined( CONFIG_LWM2M_CLIENT_UTILS_SIGNAL_MEAS_INFO_OBJ_SUPPORT )
    k_work_init_delayable( &ncell_meas_work, ncell_meas_work_handler );
    k_work_schedule( &ncell_meas_work, K_SECONDS( 1 ) );
    #endif
    #if defined( CONFIG_LWM2M_CLIENT_UTILS_VISIBLE_WIFI_AP_OBJ_SUPPORT )
    k_work_init_delayable( &ground_fix_work, ground_fix_work_handler );
    k_work_schedule( &ground_fix_work, K_SECONDS( 60 ) );
    #if defined( CONFIG_LWM2M_CLIENT_UTILS_WIFI_AP_SCANNER )
    lwm2m_wifi_request_scan();
    #endif
    #endif
    #if defined( CONFIG_APP_LWM2M_CONFORMANCE_TESTING )
    k_work_init_delayable( &send_periodical_work, send_periodically_work_handler );
    #endif

    while( true )
    {
        #if defined( CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP )
        bootstrap_flags = LWM2M_RD_CLIENT_FLAG_BOOTSTRAP;
        #else
        bootstrap_flags = 0;
        #endif

        k_mutex_lock( &lte_mutex, K_FOREVER );

        switch( client_state )
        {
            case START:
                LOG_INF( "Client connect to server" );
                ret = lwm2m_rd_client_start( &client, endpoint_name, bootstrap_flags,
                                             rd_client_event, NULL );

                if( ret )
                {
                    state_trigger_and_unlock( NETWORK_ERROR );
                }
                else
                {
                    state_trigger_and_unlock( CONNECTING );
                }

                break;

            case BOOTSTRAP:
                state_set_and_unlock( BOOTSTRAP );
                LOG_INF( "LwM2M is bootstrapping" );
                #if defined( CONFIG_BOARD_THINGY91_NRF9160_NS )
                if( ledRed.port )
                {
                    gpio_pin_set_dt( &ledRed, 0 );
                }

                if( ledBlue.port )
                {
                    gpio_pin_set_dt( &ledBlue, 100 );
                }
                #endif /* if defined( CONFIG_BOARD_THINGY91_NRF9160_NS ) */
                break;

            case CONNECTING:
                LOG_INF( "LwM2M is connecting to server (%s)", CONFIG_LWM2M_CLIENT_UTILS_SERVER );
                k_mutex_unlock( &lte_mutex );
                break;

            case CONNECTED:

                if( !modem_connected_to_network )
                {
                    /* LTE connection down suspend LwM2M engine */
                    suspend_lwm2m_engine();
                }
                else
                {
                    k_mutex_unlock( &lte_mutex );
                    LOG_INF( "LwM2M is connected to server" );

                    if( update_session_lifetime )
                    {
                        /* Read a current server  lifetime value */
                        rd_client_update_lifetime( client.srv_obj_inst );
                    }

                    #if defined( CONFIG_APP_LWM2M_CONFORMANCE_TESTING )
                    lwm2m_register_server_send_mute_cb();
                    #endif
                    /* Get current time and date */
                    date_time_update_async( date_time_event_handler );
                }

                break;

            case LTE_OFFLINE:

                if( modem_connected_to_network )
                {
                    state_trigger_and_unlock( CONNECTING );
                    LOG_INF( "Resume LwM2M engine" );
                    ret = lwm2m_engine_resume();

                    if( ret )
                    {
                        LOG_ERR( "LwM2M engine Resume fail %d", ret );
                    }
                }
                else
                {
                    LOG_INF( "LTE Offline" );
                    k_mutex_unlock( &lte_mutex );
                }

                break;

            case NETWORK_ERROR:
                /* Stop the LwM2M engine. */
                state_trigger_and_unlock( START );
                lwm2m_rd_client_stop( &client, rd_client_event, false );

                /* Set network state to start for blocking LTE */
                if( reconnect )
                {
                    reconnect = false;

                    LOG_INF( "LwM2M restart requested. The sample will try to"
                             " re-establish network connection." );

                    /* Try to reconnect to the network. */
                    ret = lte_lc_offline();

                    if( ret < 0 )
                    {
                        LOG_ERR( "Failed to put LTE link in offline state (%d)",
                                 ret );
                    }

                    modem_connect();
                }

                #if defined( CONFIG_APP_LWM2M_CONFORMANCE_TESTING )
                k_work_cancel_delayable( &send_periodical_work );
                send_count = 0;
                #endif
                break;
        }

        /* Wait for statmachine update event */
        k_sem_take( &state_mutex, K_FOREVER );
    }

    return 0;
}
