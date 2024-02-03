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
#include "core2foraws.h"
#include "unit_v_meter.h"

static const char *_TAG = "UNIT_VMETER";

esp_err_t unit_vmeter_init( bool mode )
{
    esp_err_t err = ESP_OK;

    return err;
}