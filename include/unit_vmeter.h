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

#define UNIT_VMETER_ADDR      0x48

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
esp_err_t unit_vmeter_reading_get( int16_t *millivolts );

/** 
 * @brief Set the reading mode of the V-Meter unit.
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
esp_err_t unit_vmeter_mode_set( ads111x_mode_t mode );

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
esp_err_t unit_vmeter_init( ads111x_mode_t mode );

#ifdef __cplusplus
}
#endif
#endif
