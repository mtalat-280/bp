/******************************************************************************
 * @file    main.c
 * @brief   1NCE FOTA Mender Demo
 * @details Demonstrates firmware update over-the-air using Mender and a custom
 *          FOTA module, built on Nordic Semiconductor's modem and DFU samples.
 *          Integrates LED signaling, image confirmation, and reboot scheduling
 *          for Thingy:91 and nRF9160-based platforms.
 *
 * @copyright
 *     Portions copyright (c) 2019 Nordic Semiconductor ASA
 *     Modified by 1NCE GmbH (c) 2024
 * 
 * @date    10 Jan 2024
 ******************************************************************************/

// SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

/******************************************************************************
* Includes
******************************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <dfu/dfu_target_mcuboot.h>
#include <modem/nrf_modem_lib.h>
#include <custom_fota_download.h>
#include <stdio.h>
#include "update.h"
#include "nce_mender_client.h"
#include "led_control.h"

#define INIT_ATTEMPTS    3

LOG_MODULE_REGISTER( FOTA_DEMO, CONFIG_LOG_DEFAULT_LEVEL );

static void fota_dl_handler( const struct fota_download_evt * evt )
{
    switch( evt->id )
    {
        case CUSTOM_FOTA_DOWNLOAD_EVT_ERROR:
            LOG_INF( "Received error event from FOTA download handler" );

            if( evt->cause == CUSTOM_FOTA_DOWNLOAD_ERROR_CAUSE_INVALID_UPDATE )
            {
                LOG_ERR( "Firmware download failed: Invalid update package" );
                fota_stop( 0 );
            }
            else
            {
                LOG_ERR( "Firmware download failed: Network or server error" );
                fota_stop( 1 );
            }

            break;

        case CUSTOM_FOTA_DOWNLOAD_EVT_FINISHED:
            LOG_INF( "Firmware downloaded. Rebooting in 15s to apply the update..." );
            fota_done();
            break;

        default:
            LOG_WRN( "Unknown FOTA event received (id: %d)", evt->id );
            break;
    }
}

int main( void )
{
    int err, i = 0;

    LOG_INF( "1NCE FOTA Mender demo started" );
    long_led_pattern( LED_CONNECTING );
    err = nrf_modem_lib_init();

    if( err )
    {
        LOG_ERR( "Failed to initialize modem library (err: %d)", err );
        return err;
    }

    LOG_INF( "Marking image as confirmed: boot_write_img_confirmed()" );
    boot_write_img_confirmed();
    LOG_INF( "Initializing custom FOTA download module..." );
    err = custom_fota_download_init( fota_dl_handler );

    if( err != 0 )
    {
        LOG_ERR( "custom_fota_download_init() failed (err: %d)", err );
        return err;
    }

    for(i = 0; i <= INIT_ATTEMPTS; i++)
    {
        LOG_INF( "Initializing FOTA stack (attempt %d)...", i + 1 );
        err = fota_init( &( struct fota_init_params ) {
            .update_start = fota_start,
        } );

        if( err != 0 )
        {
            LOG_ERR( "FOTA init failed (err: %d). Retrying...", err );
            long_led_pattern( LED_FAILURE );
            return err;
        }
        else
        {
            break;
        }
    }
}
