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

#ifndef _UNIT_VMETER_H_
#define _UNIT_VMETER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include "ads111x.h"

#define UNIT_VMETER_ADDR                    0x49
#define UNIT_VMETER_EEPROM_ADDR             0X53

#define UNIT_VMETER_RA_CONVERSION           0x00
#define UNIT_VMETER_RA_CONFIG               0x01

#define UNIT_VMETER_PGA_6144                0x00
#define UNIT_VMETER_PGA_4096                0x01
#define UNIT_VMETER_PGA_2048                0x02  // default
#define UNIT_VMETER_PGA_1024                0x03
#define UNIT_VMETER_PGA_512                 0x04
#define UNIT_VMETER_PGA_256                 0x05

#define UNIT_VMETER_MV_6144                 0.187500F
#define UNIT_VMETER_MV_4096                 0.125000F
#define UNIT_VMETER_MV_2048                 0.062500F  // default
#define UNIT_VMETER_MV_1024                 0.031250F
#define UNIT_VMETER_MV_512                  0.015625F
#define UNIT_VMETER_MV_256                  0.007813F

#define UNIT_VMETER_RATE_8                  0x00
#define UNIT_VMETER_RATE_16                 0x01
#define UNIT_VMETER_RATE_32                 0x02
#define UNIT_VMETER_RATE_64                 0x03
#define UNIT_VMETER_RATE_128                0x04  // default
#define UNIT_VMETER_RATE_250                0x05
#define UNIT_VMETER_RATE_475                0x06
#define UNIT_VMETER_RATE_860                0x07

#define VOLTMETER_MEASURING_DIR -1

#define UNIT_VMETER_COMP_MODE_HYSTERESIS    0x00  // default
#define UNIT_VMETER_COMP_MODE_WINDOW        0x01

#define UNIT_VMETER_MODE_CONTINUOUS         0x00
#define UNIT_VMETER_MODE_SINGLESHOT         0x01  // default

#define VOLTMETER_PRESSURE_COEFFICIENT      0.015918958F

#define VOLTMETER_PAG_6144_CAL_ADDR         208
#define VOLTMETER_PAG_4096_CAL_ADDR         216
#define VOLTMETER_PAG_2048_CAL_ADDR         224
#define VOLTMETER_PAG_1024_CAL_ADDR         232
#define VOLTMETER_PAG_512_CAL_ADDR          240
#define VOLTMETER_PAG_256_CAL_ADDR          248

#define VOLTMETER_FILTER_NUMBER             10

typedef enum
{
    UNIT_VMETER_MUX_0_1 = 0, // positive = AIN0, negative = AIN1 (default)
    UNIT_VMETER_MUX_0_3,     // positive = AIN0, negative = AIN3
    UNIT_VMETER_MUX_1_3,     // positive = AIN1, negative = AIN3
    UNIT_VMETER_MUX_2_3,     // positive = AIN2, negative = AIN3
    UNIT_VMETER_MUX_0_GND,   // positive = AIN0, negative = GND
    UNIT_VMETER_MUX_1_GND,   // positive = AIN1, negative = GND
    UNIT_VMETER_MUX_2_GND,   // positive = AIN2, negative = GND
    UNIT_VMETER_MUX_3_GND,   // positive = AIN3, negative = GND
} unit_vmeter_mux_t;

typedef enum
{
    PAG_6144 = UNIT_VMETER_PGA_6144,
    PAG_4096 = UNIT_VMETER_PGA_4096,
    PAG_2048 = UNIT_VMETER_PGA_2048,  // default
    PAG_1024 = UNIT_VMETER_PGA_1024,
    PAG_512  = UNIT_VMETER_PGA_512,
    PAG_256  = UNIT_VMETER_PGA_256,
} unit_vmeter_gain_t;

typedef enum
{
    RATE_8   = UNIT_VMETER_RATE_8,
    RATE_16  = UNIT_VMETER_RATE_16,
    RATE_32  = UNIT_VMETER_RATE_32,
    RATE_64  = UNIT_VMETER_RATE_64,
    RATE_128 = UNIT_VMETER_RATE_128,  // default
    RATE_250 = UNIT_VMETER_RATE_250,
    RATE_475 = UNIT_VMETER_RATE_475,
    RATE_860 = UNIT_VMETER_RATE_860,
} unit_vmeter_rate_t;

typedef enum {
    SINGLESHOT = UNIT_VMETER_MODE_SINGLESHOT,
    CONTINUOUS = UNIT_VMETER_MODE_CONTINUOUS,
} unit_vmeter_mode_t;

/** 
 * @brief Get the latest converted reading from the VMeter.
 * If the reading is not ready, the function will return 
 * ESP_ERR_NOT_FINISHED. You will need to retry reading until
 * a measurement is ready.
 * 
 * @param millivolts Reading results in millivolts.
 * @return [esp_err_t](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/api-reference/system/esp_err.html#macros).
 *  - ESP_OK                : Success
 *  - ESP_ERR_NOT_FINISHED	: Measurement has not finished yet
 */
esp_err_t unit_vmeter_reading_get( float *millivolts );

/** 
 * @brief Set the reading mode of the V-Meter unit.
 * Use @ref UNIT_VMETER_MODE_SINGLE for a single reading. 
 * Single shot mode is a single reading. Takes a sample reading, then 
 * powers down.
 * 
 * Use @ref UNIT_VMETER_MODE_CONTINUOUS for continuous readings.
 * Continuous mode continues to take reading after reading.
 * 
 * @param mode Single-Shot = 0, Continuous = 1.
 * @return [esp_err_t](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/api-reference/system/esp_err.html#macros).
 *  - ESP_OK                : Success
 *  - ESP_ERR_INVALID_ARG	: Driver parameter error
 */
esp_err_t unit_vmeter_mode_set( unit_vmeter_mode_t mode );

/** 
 * @brief Set the gain of the V-Meter unit.
 * 
 * @param gain Preset gain values.
 * @return [esp_err_t](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/api-reference/system/esp_err.html#macros).
 *  - ESP_OK                : Success
 *  - ESP_ERR_INVALID_ARG	: Driver parameter error
 */
esp_err_t unit_vmeter_gain_set( unit_vmeter_gain_t gain );

/** 
 * @brief Set the measurement rate of the V-Meter unit.
 * 
 * @param rate Preset rate values.
 * @return [esp_err_t](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/api-reference/system/esp_err.html#macros).
 *  - ESP_OK                : Success
 *  - ESP_ERR_INVALID_ARG	: Driver parameter error
 */
esp_err_t unit_vmeter_rate_set( unit_vmeter_rate_t rate );

/** 
 * @brief Initialize the unit.
 * Single shot mode is a single reading. Takes a sample reading, then 
 * powers down.
 * 
 * Continuous mode continues to take reading after reading.
 * 
 * @param mode Single-Shot = 0, Continuous = 1.
 * @return [esp_err_t](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/api-reference/system/esp_err.html#macros).
 *  - ESP_OK                : Success
 *  - ESP_ERR_INVALID_ARG	: Driver parameter error
 */
esp_err_t unit_vmeter_init( unit_vmeter_mode_t mode );

#ifdef __cplusplus
}
#endif
#endif
