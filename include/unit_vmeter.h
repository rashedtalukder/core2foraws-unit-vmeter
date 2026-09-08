/*!
 * @brief Library for the VMeter (ADS1115) Unit by M5Stack on the Core2 for AWS
 *
 * @copyright Copyright (c) 2025 by Rashed Talukder[https://rashedtalukder.com]
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
 *
 * @version  V0.0.2
 * @date  2026-06-04
 */

#pragma once

#ifndef _UNIT_VMETER_H_
#define _UNIT_VMETER_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Device I2C addresses
#define UNIT_VMETER_ADS1115_ADDR 0x49
#define UNIT_VMETER_EEPROM_ADDR  0x53

// ADS1115 register addresses (pointer register values)
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01
#define ADS1115_REG_LO_THRESH  0x02
#define ADS1115_REG_HI_THRESH  0x03

// ADS1115 config register bit masks and shifts
#define ADS1115_CONFIG_OS_BIT       0x8000  // Bit 15: start conversion / status
#define ADS1115_CONFIG_MUX_MASK     0x7000  // Bits 14:12: input mux
#define ADS1115_CONFIG_MUX_SHIFT    12
#define ADS1115_CONFIG_PGA_MASK     0x0E00  // Bits 11:9: gain (PGA)
#define ADS1115_CONFIG_PGA_SHIFT    9
#define ADS1115_CONFIG_MODE_MASK    0x0100  // Bit 8: operating mode
#define ADS1115_CONFIG_MODE_SHIFT   8
#define ADS1115_CONFIG_DR_MASK      0x00E0  // Bits 7:5: data rate
#define ADS1115_CONFIG_DR_SHIFT     5
#define ADS1115_CONFIG_COMP_QUE_MASK 0x0003 // Bits 1:0: comparator queue

// Number of valid gain settings
#define UNIT_VMETER_GAIN_COUNT 6
// Number of valid data rate settings
#define UNIT_VMETER_RATE_COUNT 8

  /**
   * @brief ADS1115 programmable gain amplifier (PGA) full-scale range.
   *
   * Selects the input voltage range; a smaller range gives finer resolution
   * but clips larger inputs.
   */
  typedef enum
  {
    UNIT_VMETER_GAIN_6144MV = 0x00, /*!< +/-6.144 V range */
    UNIT_VMETER_GAIN_4096MV = 0x01, /*!< +/-4.096 V range */
    UNIT_VMETER_GAIN_2048MV = 0x02, /*!< +/-2.048 V range (default) */
    UNIT_VMETER_GAIN_1024MV = 0x03, /*!< +/-1.024 V range */
    UNIT_VMETER_GAIN_512MV = 0x04,  /*!< +/-0.512 V range */
    UNIT_VMETER_GAIN_256MV = 0x05   /*!< +/-0.256 V range */
  } unit_vmeter_gain_t;

  /**
   * @brief ADS1115 conversion data rate in samples per second.
   *
   * Higher rates reduce conversion latency at the cost of more noise.
   */
  typedef enum
  {
    UNIT_VMETER_RATE_8SPS = 0x00,   /*!< 8 samples per second */
    UNIT_VMETER_RATE_16SPS = 0x01,  /*!< 16 samples per second */
    UNIT_VMETER_RATE_32SPS = 0x02,  /*!< 32 samples per second */
    UNIT_VMETER_RATE_64SPS = 0x03,  /*!< 64 samples per second */
    UNIT_VMETER_RATE_128SPS = 0x04, /*!< 128 samples per second (default) */
    UNIT_VMETER_RATE_250SPS = 0x05, /*!< 250 samples per second */
    UNIT_VMETER_RATE_475SPS = 0x06, /*!< 475 samples per second */
    UNIT_VMETER_RATE_860SPS = 0x07  /*!< 860 samples per second */
  } unit_vmeter_rate_t;

  /**
   * @brief ADS1115 conversion mode.
   */
  typedef enum
  {
    UNIT_VMETER_MODE_CONTINUOUS = 0x00, /*!< Continuous conversion */
    UNIT_VMETER_MODE_SINGLESHOT = 0x01  /*!< Single-shot (power-down between) */
  } unit_vmeter_mode_t;

  /**
   * @brief Runtime VMeter configuration / state.
   */
  typedef struct
  {
    unit_vmeter_gain_t gain;   /*!< Active PGA full-scale range */
    unit_vmeter_rate_t rate;   /*!< Active conversion data rate */
    unit_vmeter_mode_t mode;   /*!< Active conversion mode */
    float calibration_factor;  /*!< Multiplier applied to raw readings */
    bool calibration_loaded;   /*!< true if EEPROM calibration was loaded */
  } unit_vmeter_config_t;

  /**
   * @brief Initialize the VMeter (ADS1115) unit.
   *
   * Registers the ADS1115 and EEPROM I2C devices, writes the initial ADS1115
   * configuration, and attempts to load factory calibration (falling back to
   * default calibration on failure). Calling again while already initialized
   * returns ESP_OK.
   *
   * @param[in] mode Initial conversion mode (single-shot or continuous).
   * @return
   *  - ESP_OK              : Success (or already initialized)
   *  - ESP_ERR_INVALID_ARG : Invalid mode value
   *  - Other               : Error from PA Hub / I2C device add / config write
   */
  esp_err_t unit_vmeter_init( unit_vmeter_mode_t mode );

  /**
   * @brief Set the PGA gain (input voltage range).
   *
   * Reloads the calibration factor for the new gain.
   *
   * @param[in] gain Gain setting.
   * @return
   *  - ESP_OK                : Success
   *  - ESP_ERR_INVALID_STATE : Not initialized
   *  - ESP_ERR_INVALID_ARG   : gain out of range
   *  - Other                 : I2C read/write error
   */
  esp_err_t unit_vmeter_set_gain( unit_vmeter_gain_t gain );

  /**
   * @brief Set the conversion data rate.
   *
   * @param[in] rate Data rate setting.
   * @return
   *  - ESP_OK                : Success
   *  - ESP_ERR_INVALID_STATE : Not initialized
   *  - ESP_ERR_INVALID_ARG   : rate out of range
   *  - Other                 : I2C read/write error
   */
  esp_err_t unit_vmeter_set_rate( unit_vmeter_rate_t rate );

  /**
   * @brief Set the conversion mode (single-shot or continuous).
   *
   * @param[in] mode Operating mode.
   * @return
   *  - ESP_OK                : Success
   *  - ESP_ERR_INVALID_STATE : Not initialized
   *  - ESP_ERR_INVALID_ARG   : Invalid mode value
   *  - Other                 : I2C read/write error
   */
  esp_err_t unit_vmeter_set_mode( unit_vmeter_mode_t mode );

  /**
   * @brief Get the latest calibrated voltage reading.
   *
   * In single-shot mode the conversion must already be complete (see
   * unit_vmeter_start_conversion() and unit_vmeter_is_converting()).
   *
   * @param[out] voltage Calibrated reading in millivolts. Must not be NULL.
   * @return
   *  - ESP_OK                : Success
   *  - ESP_ERR_INVALID_ARG   : voltage is NULL
   *  - ESP_ERR_INVALID_STATE : Not initialized
   *  - ESP_ERR_NOT_FINISHED  : Single-shot conversion not yet complete
   *  - Other                 : I2C read error
   */
  esp_err_t unit_vmeter_reading_get( float *voltage );

  /**
   * @brief Get the latest raw (uncalibrated) ADC reading.
   *
   * @param[out] raw_value Signed 16-bit ADC value. Must not be NULL.
   * @return
   *  - ESP_OK                : Success
   *  - ESP_ERR_INVALID_ARG   : raw_value is NULL
   *  - ESP_ERR_INVALID_STATE : Not initialized
   *  - ESP_ERR_NOT_FINISHED  : Single-shot conversion not yet complete
   *  - Other                 : I2C read error
   */
  esp_err_t unit_vmeter_raw_reading_get( int16_t *raw_value );

  /**
   * @brief Check whether a conversion is currently in progress.
   *
   * @return true if a conversion is in progress (or on read error / not
   *         initialized), false if a result is ready.
   */
  bool unit_vmeter_is_converting( void );

  /**
   * @brief Start a single conversion (single-shot mode only).
   *
   * @return
   *  - ESP_OK                : Conversion started
   *  - ESP_ERR_INVALID_STATE : Not initialized, or not in single-shot mode
   *  - Other                 : I2C read/write error
   */
  esp_err_t unit_vmeter_start_conversion( void );

  /**
   * @brief (Re)load calibration data from the unit's EEPROM for the active
   * gain.
   *
   * @return
   *  - ESP_OK                : Success
   *  - ESP_ERR_INVALID_STATE : Not initialized
   *  - ESP_ERR_INVALID_CRC   : EEPROM calibration checksum mismatch
   *  - Other                 : I2C read error
   */
  esp_err_t unit_vmeter_load_calibration( void );

  esp_err_t unit_vmeter_conversion_ready( bool *ready );
  esp_err_t unit_vmeter_get_config( unit_vmeter_config_t *config );
  esp_err_t unit_vmeter_deinit( void );

#ifdef __cplusplus
}
#endif

#endif