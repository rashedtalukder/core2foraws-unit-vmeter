/*!
 * @brief Library for the Voltmeter unit by M5Stack on the Core2 for AWS
 * @copyright Copyright (c) 2024 Rashed Talukder[https://rashedtalukder.com]
 * 
 * @license SPDX-License-Identifier: Apache 2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @Links [VMeter](https://docs.m5stack.com/en/unit/vmeter)
 * @version  V0.0.1
 * @date  2024-02-03
 */

#include <stdio.h>
#include <stdbool.h>
#include <esp_log.h>
#include "ads111x.h"
#include "core2foraws.h"
#include "unit_v_meter.h"

#define UNIT_VMETER_DATA_RATE    ADS111X_DATA_RATE_128
#define UNIT_VMETER_MUX          ADS111X_MUX_0_GND

static const char *_TAG = "UNIT_VMETER";
static const float _gain_val = ads111x_gain_values[ ADS111X_GAIN_4V096 ];
static const i2c_dev_t _dev;

esp_err_t unit_vmeter_reading_get( int16_t *millivolts )
{
    esp_err_t err = ESP_OK;

    bool busy;
    err = ads111x_is_busy( &_dev, &busy );

    if( busy )
    {
        err = ESP_ERR_NOT_FINISHED;
    }
    else
    {
        int16_t raw_volts;
        err = ads111x_get_value( &_dev, &raw_volts );
        if ( err == ESP_OK )
        {
            ESP_LOGD( _TAG, "Read ADS1115 raw value: %d", raw_volts );
            millivolts = _gain_val / ADS111X_MAX_VALUE * raw;
        }
    }

    return err;

}

esp_err_t unit_vmeter_mode_set( ads111x_mode_t mode )
{
    esp_err_t err = ESP_OK;
    ads111x_set_mode( &_dev, ADS111X_MODE_CONTINUOUS );
    ESP_LOGD( _TAG, "Set ADS1115 to %s", mode ? "single-shot mode" : "continuous mode" );

    return err;
}

esp_err_t unit_vmeter_init( ads111x_mode_t mode )
{
    esp_err_t err = ESP_OK;

    memset( &_dev, 0, sizeof( i2c_dev_t ) );
    ads111x_init_desc( &_dev, UNIT_VMETER_ADDR, COMMON_I2C_EXTERNAL, PORT_A_SDA_PIN, PORT_A_SCL_PIN );
    ESP_LOGI( _TAG, "Initialized ADS1115 initial device descriptor for address 0x%x", UNIT_VMETER_ADDR );

    unit_vmeter_mode_set( mode );
    ESP_LOGD( _TAG, "Set ADS1115 to %s", mode ? "single-shot mode" : "continuous mode" );

    ads111x_set_data_rate( &_dev, UNIT_VMETER_DATA_RATE );
    ESP_LOGD( _TAG, "Set ADS1115 data rate to enum #%i", UNIT_VMETER_DATA_RATE );

    ads111x_set_input_mux( &_dev, UNIT_VMETER_MUX );
    ads111x_set_gain( &_dev, _gain_val );
    ESP_LOGD( _TAG, "Set ADS1115 gain to %f", _gain_val );

    return err;
}