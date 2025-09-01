/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/net/socket.h>
#include <custom_fota_download.h>
#include <nrf_socket.h>
#include <stdio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/toolchain.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include "nce_mender_client.h"
#include <modem/nrf_modem_lib.h>
#include "update.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER( MODEM_FOTA, CONFIG_LOG_DEFAULT_LEVEL );

static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET( DT_ALIAS( sw0 ), gpios );
static K_SEM_DEFINE( lte_connected_sem, 0, 1 );

/**
 * @brief Handler for LTE link control events
 */
static void lte_lc_handler( const struct lte_lc_evt *const evt )
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

        default:
            break;
    }
}
/* static function declaration */
static void dfu_button_pressed( const struct device * gpiob,
                                struct gpio_callback * cb,
                                uint32_t pins );

static struct gpio_callback sw0_cb;

int cert_provision( void )
{
    static const char cert[] =
    {
        #include "../cert/GTS_Root_R4"
    };

    BUILD_ASSERT( sizeof( cert ) < KB( 4 ), "Certificate too large" );

    int err;
    bool exists;

    err = modem_key_mgmt_exists( TLS_SEC_TAG,
                                 MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists );

    if( err )
    {
        LOG_ERR( "Failed to check for certificates (err %d)", err );
        return err;
    }

    if( exists )
    {
        /* For the sake of simplicity we delete what is provisioned
         * with our security tag and reprovision our certificate.
         */
        err = modem_key_mgmt_delete( TLS_SEC_TAG,
                                     MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN );

        if( err )
        {
            LOG_ERR( "Failed to delete existing certificate (err %d)", err );
        }
    }

    LOG_INF( "Provisioning certificate" );

    /*  Provision certificate to the modem */
    err = modem_key_mgmt_write( TLS_SEC_TAG,
                                MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
                                sizeof( cert ) - 1 );

    if( err )
    {
        LOG_ERR( "Failed to provision certificate (err %d)", err );
        return err;
    }

    return 0;
}

int button_init( void )
{
    int err;

    if( !device_is_ready( sw0.port ) )
    {
        LOG_ERR( "SW0 GPIO port device not ready" );
        return -ENODEV;
    }

    err = gpio_pin_configure_dt( &sw0, GPIO_INPUT );

    if( err < 0 )
    {
        return err;
    }

    gpio_init_callback( &sw0_cb, dfu_button_pressed, BIT( sw0.pin ) );

    err = gpio_add_callback( sw0.port, &sw0_cb );

    if( err < 0 )
    {
        LOG_ERR( "Unable to configure SW0 GPIO pin!" );
        return err;
    }

    button_irq_enable();

    return 0;
}
void button_irq_disable( void )
{
    gpio_pin_interrupt_configure_dt( &sw0, GPIO_INT_DISABLE );
}
void button_irq_enable( void )
{
    gpio_pin_interrupt_configure_dt( &sw0, GPIO_INT_EDGE_TO_ACTIVE );
}
static void dfu_button_pressed( const struct device * gpiob,
                                struct gpio_callback * cb,
                                uint32_t pins )
{
    button_irq_disable();
}

/**
 * @brief Configures modem to provide LTE link.
 */
int modem_configure_and_connect( void )
{
    int err;

    #if defined( CONFIG_USE_HTTPS )
    err = cert_provision();

    if( err )
    {
        LOG_ERR( "Could not provision root CA to sec tag %d", TLS_SEC_TAG );
        return err;
    }
    #endif /* CONFIG_USE_HTTPS */

    LOG_INF( "LTE Link Connecting ..." );
    err = lte_lc_connect_async( lte_lc_handler );

    if( err )
    {
        LOG_ERR( "LTE link could not be established." );
        return err;
    }

    k_sem_take( &lte_connected_sem, K_FOREVER );
    return 0;
}

static int shell_download( const struct shell * shell,
                           size_t argc,
                           char ** argv )
{
    ARG_UNUSED( argc );
    ARG_UNUSED( argv );

    fota_start();

    return 0;
}

static int shell_reboot( const struct shell * shell,
                         size_t argc,
                         char ** argv )
{
    ARG_UNUSED( argc );
    ARG_UNUSED( argv );

    shell_print( shell, "Device will now reboot" );
    sys_reboot( SYS_REBOOT_WARM );

    return 0;
}

SHELL_CMD_REGISTER( reset, NULL, "For rebooting device", shell_reboot );
SHELL_CMD_REGISTER( download, NULL, "For downloading modem  firmware", shell_download );
