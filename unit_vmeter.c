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
 * @version  V0.0.1
 * @date  2025-06-09
 */

#include "unit_vmeter.h"
#include "core2foraws_expports.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "UNIT_VMETER";

// Resolution values for each gain setting (mV per LSB)
static const float GAIN_RESOLUTIONS[] = {
    0.187500F, // ±6.144V
    0.125000F, // ±4.096V
    0.062500F, // ±2.048V
    0.031250F, // ±1.024V
    0.015625F, // ±0.512V
    0.007813F  // ±0.256V
};

// EEPROM calibration addresses for each gain
static const uint8_t GAIN_CAL_ADDRESSES[] = {
    208, // PAG_6144
    216, // PAG_4096
    224, // PAG_2048
    232, // PAG_1024
    240, // PAG_512
    248  // PAG_256
};

// Pressure coefficient for voltage calculation
#define VMETER_PRESSURE_COEFFICIENT 0.015918958F
#define VMETER_MEASURING_DIR        -1

static unit_vmeter_config_t vmeter_config = { .gain = UNIT_VMETER_GAIN_2048MV,
                                              .rate = UNIT_VMETER_RATE_128SPS,
                                              .mode =
                                                  UNIT_VMETER_MODE_SINGLESHOT,
                                              .calibration_factor = 1.0f,
                                              .calibration_loaded = false };

static bool vmeter_initialized = false;

static esp_err_t vmeter_read_register( uint8_t reg, uint16_t *value )
{
  uint8_t data[ 2 ];
  esp_err_t err =
      core2foraws_expports_i2c_read( UNIT_VMETER_ADS1115_ADDR, reg, data, 2 );
  if( err == ESP_OK )
  {
    *value = ( data[ 0 ] << 8 ) | data[ 1 ];
  }
  return err;
}

static esp_err_t vmeter_write_register( uint8_t reg, uint16_t value )
{
  uint8_t data[ 2 ] = { value >> 8, value & 0xFF };
  return core2foraws_expports_i2c_write( UNIT_VMETER_ADS1115_ADDR, reg, data,
                                         2 );
}

static esp_err_t vmeter_eeprom_read( uint8_t address, uint8_t *buffer,
                                     uint8_t length )
{
  return core2foraws_expports_i2c_read( UNIT_VMETER_EEPROM_ADDR, address,
                                        buffer, length );
}

static float vmeter_get_resolution( unit_vmeter_gain_t gain )
{
  return GAIN_RESOLUTIONS[ gain ] / VMETER_PRESSURE_COEFFICIENT;
}

static esp_err_t vmeter_load_calibration_for_gain( unit_vmeter_gain_t gain )
{
  uint8_t cal_addr = GAIN_CAL_ADDRESSES[ gain ];
  uint8_t buffer[ 8 ] = { 0 };

  esp_err_t err = vmeter_eeprom_read( cal_addr, buffer, 8 );
  if( err != ESP_OK )
  {
    ESP_LOGW( TAG, "Failed to read calibration from EEPROM" );
    return err;
  }

  // Verify checksum
  uint8_t checksum = 0;
  for( int i = 0; i < 5; i++ )
  {
    checksum ^= buffer[ i ];
  }

  if( checksum != buffer[ 5 ] )
  {
    ESP_LOGW( TAG, "Calibration checksum mismatch for gain %d", gain );
    vmeter_config.calibration_factor = 1.0f;
    return ESP_ERR_INVALID_CRC;
  }

  // Extract calibration values
  int16_t hope = ( buffer[ 1 ] << 8 ) | buffer[ 2 ];
  int16_t actual = ( buffer[ 3 ] << 8 ) | buffer[ 4 ];

  if( actual != 0 )
  {
    vmeter_config.calibration_factor = (float)hope / actual;
    vmeter_config.calibration_loaded = true;
    ESP_LOGI( TAG, "Loaded calibration: hope=%d, actual=%d, factor=%.4f", hope,
              actual, vmeter_config.calibration_factor );
  }
  else
  {
    vmeter_config.calibration_factor = 1.0f;
    ESP_LOGW( TAG, "Invalid calibration data (actual=0)" );
  }

  return ESP_OK;
}

esp_err_t unit_vmeter_init( unit_vmeter_mode_t mode )
{
  if( vmeter_initialized )
  {
    return ESP_OK;
  }

  // Set initial configuration
  vmeter_config.mode = mode;

  // Configure ADS1115
  uint16_t config = 0x8000 |         // Start bit (for single-shot)
                    ( 0x00 << 12 ) | // MUX: AIN0-AIN1 (differential)
                    ( vmeter_config.gain << 9 ) | ( vmeter_config.mode << 8 ) |
                    ( vmeter_config.rate << 5 ) |
                    ( 0x00 << 4 ) | // Comparator mode
                    ( 0x00 << 3 ) | // Comparator polarity
                    ( 0x00 << 2 ) | // Latching comparator
                    ( 0x03 << 0 );  // Comparator queue

  esp_err_t err = vmeter_write_register( ADS1115_REG_CONFIG, config );
  if( err != ESP_OK )
  {
    ESP_LOGE( TAG, "Failed to configure ADS1115: %s", esp_err_to_name( err ) );
    return err;
  }

  // Load calibration data
  err = vmeter_load_calibration_for_gain( vmeter_config.gain );
  if( err != ESP_OK )
  {
    ESP_LOGW( TAG, "Using default calibration" );
  }

  vmeter_initialized = true;
  ESP_LOGI( TAG, "VMeter initialized successfully" );

  return ESP_OK;
}

esp_err_t unit_vmeter_set_gain( unit_vmeter_gain_t gain )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Update gain bits
  config &= ~( 0x07 << 9 );
  config |= ( gain << 9 );

  err = vmeter_write_register( ADS1115_REG_CONFIG, config );
  if( err == ESP_OK )
  {
    vmeter_config.gain = gain;
    // Reload calibration for new gain
    vmeter_load_calibration_for_gain( gain );
  }

  return err;
}

esp_err_t unit_vmeter_set_rate( unit_vmeter_rate_t rate )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Update rate bits
  config &= ~( 0x07 << 5 );
  config |= ( rate << 5 );

  err = vmeter_write_register( ADS1115_REG_CONFIG, config );
  if( err == ESP_OK )
  {
    vmeter_config.rate = rate;
  }

  return err;
}

esp_err_t unit_vmeter_set_mode( unit_vmeter_mode_t mode )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Update mode bit
  config &= ~( 0x01 << 8 );
  config |= ( mode << 8 );

  err = vmeter_write_register( ADS1115_REG_CONFIG, config );
  if( err == ESP_OK )
  {
    vmeter_config.mode = mode;
  }

  return err;
}

bool unit_vmeter_is_converting( void )
{
  if( !vmeter_initialized )
  {
    return false;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return false;
  }

  // Bit 15: 0 = converting, 1 = not converting
  return ( config & 0x8000 ) == 0;
}

esp_err_t unit_vmeter_start_conversion( void )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  if( vmeter_config.mode != UNIT_VMETER_MODE_SINGLESHOT )
  {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Set start bit
  config |= 0x8000;

  return vmeter_write_register( ADS1115_REG_CONFIG, config );
}

esp_err_t unit_vmeter_raw_reading_get( int16_t *raw_value )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  if( vmeter_config.mode == UNIT_VMETER_MODE_SINGLESHOT )
  {
    if( unit_vmeter_is_converting() )
    {
      return ESP_ERR_NOT_FINISHED;
    }
  }

  uint16_t conversion;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONVERSION, &conversion );
  if( err == ESP_OK )
  {
    *raw_value = (int16_t)conversion;
  }

  return err;
}

esp_err_t unit_vmeter_reading_get( float *voltage )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  int16_t raw_value;
  esp_err_t err = unit_vmeter_raw_reading_get( &raw_value );
  if( err != ESP_OK )
  {
    return err;
  }

  float resolution = vmeter_get_resolution( vmeter_config.gain );
  *voltage = resolution * vmeter_config.calibration_factor * raw_value *
             VMETER_MEASURING_DIR;

  return ESP_OK;
}

esp_err_t unit_vmeter_load_calibration( void )
{
  if( !vmeter_initialized )
  {
    return ESP_ERR_INVALID_STATE;
  }

  return vmeter_load_calibration_for_gain( vmeter_config.gain );
}
