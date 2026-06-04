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

#include "unit_vmeter.h"
#include "core2foraws_expports.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
#include "unit_pahub.h"
#endif

static const char *TAG = "UNIT_VMETER";

// Resolution values for each gain setting (mV per LSB)
// From ADS1115 datasheet: LSB = FSR / 32768
static const float GAIN_RESOLUTIONS[] = {
    0.1875000F, // ±6.144V: 187.5 µV
    0.1250000F, // ±4.096V: 125.0 µV
    0.0625000F, // ±2.048V:  62.5 µV
    0.0312500F, // ±1.024V:  31.25 µV
    0.0156250F, // ±0.512V:  15.625 µV
    0.0078125F  // ±0.256V:   7.8125 µV
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

// Board voltage divider ratio: converts ADC input voltage to external voltage.
// The VMeter board uses a resistor divider (680kΩ / 11kΩ) so the ADC sees
// a fraction of the actual measured voltage. Dividing the ADC resolution by
// this ratio gives the resolution in terms of external voltage.
#define VMETER_DIVIDER_RATIO 0.015918958F

// The differential inputs on the board are wired so that positive external
// voltage produces a negative ADC reading. This constant flips the sign.
#define VMETER_SIGN_CORRECTION -1

static unit_vmeter_config_t vmeter_config = { .gain = UNIT_VMETER_GAIN_2048MV,
                                              .rate = UNIT_VMETER_RATE_128SPS,
                                              .mode =
                                                  UNIT_VMETER_MODE_SINGLESHOT,
                                              .calibration_factor = 1.0f,
                                              .calibration_loaded = false };

static bool vmeter_initialized = false;
static i2c_master_dev_handle_t _ads1115_dev = NULL;
static i2c_master_dev_handle_t _eeprom_dev = NULL;

static esp_err_t vmeter_read_register( uint8_t reg, uint16_t *value )
{
  uint8_t data[ 2 ];
  esp_err_t err;

#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  err = unit_pahub_i2c_read( CONFIG_UNIT_VMETER_PAHUB_CHANNEL,
                             _ads1115_dev, reg, data, 2 );
#else
  err = core2foraws_expports_i2c_read( _ads1115_dev, reg, data, 2 );
#endif

  if( err == ESP_OK )
  {
    *value = ( data[ 0 ] << 8 ) | data[ 1 ];
  }
  return err;
}

static esp_err_t vmeter_write_register( uint8_t reg, uint16_t value )
{
  uint8_t data[ 2 ] = { value >> 8, value & 0xFF };

#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  return unit_pahub_i2c_write( CONFIG_UNIT_VMETER_PAHUB_CHANNEL,
                               _ads1115_dev, reg, data, 2 );
#else
  return core2foraws_expports_i2c_write( _ads1115_dev, reg, data,
                                         2 );
#endif
}

static esp_err_t vmeter_eeprom_read( uint8_t address, uint8_t *buffer,
                                     uint8_t length )
{
#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  return unit_pahub_i2c_read( CONFIG_UNIT_VMETER_PAHUB_CHANNEL,
                              _eeprom_dev, address, buffer,
                              length );
#else
  return core2foraws_expports_i2c_read( _eeprom_dev, address,
                                        buffer, length );
#endif
}

static float vmeter_get_resolution( unit_vmeter_gain_t gain )
{
  return GAIN_RESOLUTIONS[ gain ] / VMETER_DIVIDER_RATIO;
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

  if( mode != UNIT_VMETER_MODE_CONTINUOUS &&
      mode != UNIT_VMETER_MODE_SINGLESHOT )
  {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err;

#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  err = unit_pahub_init();
  if( err != ESP_OK )
  {
    ESP_LOGE( TAG, "PA Hub initialization failed: %s", esp_err_to_name( err ) );
    return err;
  }

  err = unit_pahub_channel_set( CONFIG_UNIT_VMETER_PAHUB_CHANNEL );
  if( err != ESP_OK )
  {
    ESP_LOGE( TAG, "PA Hub channel set failed: %s", esp_err_to_name( err ) );
    return err;
  }
#endif

  err = core2foraws_expports_i2c_device_add( UNIT_VMETER_ADS1115_ADDR, 100000, &_ads1115_dev );
  if( err != ESP_OK )
  {
    ESP_LOGE( TAG, "Failed to add ADS1115 I2C device: %s", esp_err_to_name( err ) );
    return err;
  }

  err = core2foraws_expports_i2c_device_add( UNIT_VMETER_EEPROM_ADDR, 100000, &_eeprom_dev );
  if( err != ESP_OK )
  {
    ESP_LOGE( TAG, "Failed to add EEPROM I2C device: %s", esp_err_to_name( err ) );
    return err;
  }

  // Set initial configuration
  vmeter_config.mode = mode;

  // Build the ADS1115 config register value.
  // See datasheet section 7.3 for the bit layout.
  uint16_t config =
      ADS1115_CONFIG_OS_BIT |                           // Start a conversion
      ( 0x00 << ADS1115_CONFIG_MUX_SHIFT ) |            // MUX: AIN0-AIN1 differential
      ( vmeter_config.gain << ADS1115_CONFIG_PGA_SHIFT ) |
      ( vmeter_config.mode << ADS1115_CONFIG_MODE_SHIFT ) |
      ( vmeter_config.rate << ADS1115_CONFIG_DR_SHIFT ) |
      0x03;                                             // COMP_QUE: disable comparator

  err = vmeter_write_register( ADS1115_REG_CONFIG, config );
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

  if( gain >= UNIT_VMETER_GAIN_COUNT )
  {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Clear OS bit so we don't accidentally start a conversion.
  // Per the ADS1115 datasheet, the OS bit reads as 1 when idle.
  // Writing it back as 1 would start an unwanted conversion.
  config &= ~ADS1115_CONFIG_OS_BIT;

  // Update gain (PGA) bits [11:9]
  config &= ~ADS1115_CONFIG_PGA_MASK;
  config |= ( gain << ADS1115_CONFIG_PGA_SHIFT );

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

  if( rate >= UNIT_VMETER_RATE_COUNT )
  {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Clear OS bit so we don't accidentally start a conversion
  config &= ~ADS1115_CONFIG_OS_BIT;

  // Update data rate bits [7:5]
  config &= ~ADS1115_CONFIG_DR_MASK;
  config |= ( rate << ADS1115_CONFIG_DR_SHIFT );

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

  if( mode != UNIT_VMETER_MODE_CONTINUOUS &&
      mode != UNIT_VMETER_MODE_SINGLESHOT )
  {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t config;
  esp_err_t err = vmeter_read_register( ADS1115_REG_CONFIG, &config );
  if( err != ESP_OK )
  {
    return err;
  }

  // Clear OS bit so we don't accidentally start a conversion
  config &= ~ADS1115_CONFIG_OS_BIT;

  // Update mode bit [8]
  config &= ~ADS1115_CONFIG_MODE_MASK;
  config |= ( mode << ADS1115_CONFIG_MODE_SHIFT );

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
  return ( config & ADS1115_CONFIG_OS_BIT ) == 0;
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

  // Set OS bit to start a single conversion
  config |= ADS1115_CONFIG_OS_BIT;

  return vmeter_write_register( ADS1115_REG_CONFIG, config );
}

esp_err_t unit_vmeter_raw_reading_get( int16_t *raw_value )
{
  if( raw_value == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }

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
  if( voltage == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }

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
             VMETER_SIGN_CORRECTION;

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
