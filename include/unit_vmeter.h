/*!
 * @brief Library for the VMeter (ADS1115) Unit by M5Stack on the Core2 for AWS
 *
 * @copyright Copyright (c) 2025 by Rashed Talukder[https://rashedtalukder.com]
 *
 * @license SPDX-License-Identifier: Apache-2.0
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
 * @version  V1.0.0
 * @date  2026-09-26
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

#define UNIT_VMETER_ADS1115_ADDR 0x49
#define UNIT_VMETER_EEPROM_ADDR  0x53

/** Rated module input range, +/- millivolts (M5Stack product page). */
#define UNIT_VMETER_RATED_RANGE_MV 36000

#define UNIT_VMETER_GAIN_COUNT 6
#define UNIT_VMETER_RATE_COUNT 8

  /**
   * @brief ADS1115 PGA full-scale range at the ADC input.
   *
   * The board divider scales these to external ranges of roughly +/-386 V,
   * +/-257 V, +/-129 V, +/-64 V, +/-32 V and +/-16 V; the module itself is
   * rated for +/-36 V only. PGA codes 6 and 7 alias 0.256 V and are not used.
   */
  typedef enum
  {
    UNIT_VMETER_GAIN_6144MV = 0,
    UNIT_VMETER_GAIN_4096MV = 1,
    UNIT_VMETER_GAIN_2048MV = 2,
    UNIT_VMETER_GAIN_1024MV = 3, /*!< Smallest range covering +/-36 V (default) */
    UNIT_VMETER_GAIN_512MV = 4,
    UNIT_VMETER_GAIN_256MV = 5
  } unit_vmeter_gain_t;

  /** @brief ADS1115 data rate; the chip tolerance is +/-10 %. */
  typedef enum
  {
    UNIT_VMETER_RATE_8SPS = 0,
    UNIT_VMETER_RATE_16SPS = 1,
    UNIT_VMETER_RATE_32SPS = 2,
    UNIT_VMETER_RATE_64SPS = 3,
    UNIT_VMETER_RATE_128SPS = 4, /*!< Default */
    UNIT_VMETER_RATE_250SPS = 5,
    UNIT_VMETER_RATE_475SPS = 6,
    UNIT_VMETER_RATE_860SPS = 7
  } unit_vmeter_rate_t;

  typedef enum
  {
    UNIT_VMETER_MODE_CONTINUOUS = 0, /*!< Free-running at the data rate */
    UNIT_VMETER_MODE_SINGLESHOT = 1  /*!< Powered down between conversions (default) */
  } unit_vmeter_mode_t;

  typedef struct
  {
    unit_vmeter_gain_t gain;
    unit_vmeter_rate_t rate;
    unit_vmeter_mode_t mode;
  } unit_vmeter_settings_t;

#define UNIT_VMETER_SETTINGS_DEFAULT()                                         \
  ( (unit_vmeter_settings_t){ .gain = UNIT_VMETER_GAIN_1024MV,                 \
                              .rate = UNIT_VMETER_RATE_128SPS,                 \
                              .mode = UNIT_VMETER_MODE_SINGLESHOT } )

  typedef struct
  {
    int16_t raw;          /*!< ADC code (two's complement) */
    float millivolts;     /*!< External terminal voltage */
    int64_t timestamp_us; /*!< esp_timer time the result was read */
    bool calibrated;      /*!< Factory calibration applied */
    bool over_range;      /*!< ADC clipped or outside the rated +/-36 V */
  } unit_vmeter_sample_t;

  typedef struct
  {
    unit_vmeter_settings_t settings;
    float full_scale_mv;        /*!< External +/- range of the active gain */
    float resolution_mv;        /*!< External mV per LSB, calibration included */
    float calibration_factor;   /*!< Factory gain factor, 1.0 when unavailable */
    bool calibrated;            /*!< Active gain has a valid EEPROM block */
    uint8_t calibration_mask;   /*!< Bit n set when gain n has a valid block */
    bool alert_enabled;
    uint32_t i2c_errors;        /*!< Failed transfers after retries */
    uint32_t device_resets;     /*!< Unexpected ADS1115 resets recovered */
  } unit_vmeter_info_t;

  /** @brief Hardware window-comparator alert, in external millivolts. */
  typedef struct
  {
    float low_mv;        /*!< Alert when the input falls below this */
    float high_mv;       /*!< Alert when the input rises above this */
    uint8_t consecutive; /*!< Conversions beyond a limit before alerting: 1, 2 or 4 */
  } unit_vmeter_alert_config_t;

#define UNIT_VMETER_ALERT_LOW  ( 1u << 0 )
#define UNIT_VMETER_ALERT_HIGH ( 1u << 1 )

  /**
   * @brief Initialize the unit on Port A (or the configured PaHub channel).
   *
   * Call core2foraws_expports_i2c_begin() first. Writes and verifies the
   * ADS1115 configuration and loads every factory calibration block from the
   * read-only EEPROM. Missing or invalid calibration is not fatal; see
   * unit_vmeter_info_get(). Call before sharing the driver between tasks;
   * all other functions are thread-safe.
   *
   * @param[in] settings Initial settings, or NULL for
   *                     UNIT_VMETER_SETTINGS_DEFAULT().
   * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE (already
   *         initialized), ESP_ERR_NO_MEM, ESP_ERR_INVALID_RESPONSE (config
   *         readback mismatch) or an I2C error.
   */
  esp_err_t unit_vmeter_init( const unit_vmeter_settings_t *settings );

  /**
   * @brief Power the ADS1115 down, disable the comparator and release the bus
   * devices. A failed release is retried by the next call.
   */
  esp_err_t unit_vmeter_deinit( void );

  /**
   * @brief Apply gain, rate and mode in one verified register write.
   *
   * Invalidates older results. Active alert thresholds are recomputed for
   * the new gain.
   */
  esp_err_t unit_vmeter_configure( const unit_vmeter_settings_t *settings );
  esp_err_t unit_vmeter_set_gain( unit_vmeter_gain_t gain );
  esp_err_t unit_vmeter_set_rate( unit_vmeter_rate_t rate );
  esp_err_t unit_vmeter_set_mode( unit_vmeter_mode_t mode );

  esp_err_t unit_vmeter_info_get( unit_vmeter_info_t *info );

  /**
   * @brief Blocking measurement that always returns a new conversion.
   *
   * Single-shot: triggers, sleeps for the conversion time with microsecond
   * resolution, confirms completion and reads. Continuous: waits until a
   * conversion newer than the previous result (and the last configuration
   * change) has completed. An unexpected ADS1115 reset is repaired and the
   * measurement repeated once.
   *
   * @return ESP_OK; ESP_ERR_INVALID_RESPONSE when @p sample->over_range is set
   *         (sample still filled) or the unit keeps resetting;
   *         ESP_ERR_TIMEOUT; or an I2C error.
   */
  esp_err_t unit_vmeter_measure( unit_vmeter_sample_t *sample );

  /**
   * @brief Non-blocking single-shot trigger.
   *
   * @return ESP_OK, ESP_ERR_NOT_FINISHED while the previous conversion may
   *         still be running, ESP_ERR_INVALID_STATE in continuous mode, or an
   *         I2C error.
   */
  esp_err_t unit_vmeter_start_conversion( void );

  /**
   * @brief Report whether a result can be read without blocking.
   *
   * Single-shot: true after a started conversion finished. Continuous: true
   * once a conversion with the current settings exists.
   *
   * @return ESP_OK, ESP_ERR_INVALID_RESPONSE after a recovered unit reset
   *         (start a new conversion), or an I2C error.
   */
  esp_err_t unit_vmeter_conversion_ready( bool *ready );

  /**
   * @brief Non-blocking read of the completed (single-shot) or latest
   * (continuous) conversion.
   *
   * @return As unit_vmeter_measure(), plus ESP_ERR_NOT_FINISHED when no
   *         result is available yet.
   */
  esp_err_t unit_vmeter_read( unit_vmeter_sample_t *sample );

  /**
   * @brief Enable the ADS1115 latching window comparator.
   *
   * The chip checks every conversion, so continuous mode monitors the input
   * autonomously between host polls. ALERT/RDY is not wired to the
   * connector; events are collected over I2C with the SMBus alert response
   * (address 0x0C), which also clears the latch of any other alerting device
   * on the same bus segment. Limits beyond the active full scale disable that
   * side.
   *
   * @return ESP_OK, ESP_ERR_INVALID_ARG (non-finite, low >= high, window
   *         narrower than one LSB, or consecutive not 1/2/4) or an I2C error.
   */
  esp_err_t unit_vmeter_alert_enable( const unit_vmeter_alert_config_t *config );
  esp_err_t unit_vmeter_alert_disable( void );

  /**
   * @brief Return and clear the UNIT_VMETER_ALERT_* events latched since the
   * previous call. Reads of conversion data collect pending events first so
   * none are lost.
   */
  esp_err_t unit_vmeter_alert_get( uint8_t *alerts );

  /**
   * @brief Issue an I2C general-call reset, then restore the configuration,
   * thresholds and calibration.
   *
   * @warning Every general-call-capable device on the same bus segment is
   * reset as well.
   */
  esp_err_t unit_vmeter_reset( void );

#ifdef __cplusplus
}
#endif
#endif
