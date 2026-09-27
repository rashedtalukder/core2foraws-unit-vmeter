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

#include "unit_vmeter.h"
#include "core2foraws_expports.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
#include "unit_pahub.h"
#endif

#ifndef CONFIG_UNIT_VMETER_I2C_SPEED_HZ
#define CONFIG_UNIT_VMETER_I2C_SPEED_HZ 400000
#endif
#ifndef CONFIG_UNIT_VMETER_I2C_RETRIES
#define CONFIG_UNIT_VMETER_I2C_RETRIES 2
#endif
#ifndef CONFIG_UNIT_VMETER_HEALTH_CHECK_MS
#define CONFIG_UNIT_VMETER_HEALTH_CHECK_MS 1000
#endif

static const char *TAG = "UNIT_VMETER";

#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01
#define ADS1115_REG_LO_THRESH  0x02
#define ADS1115_REG_HI_THRESH  0x03

#define ADS1115_CFG_OS          0x8000u
#define ADS1115_CFG_PGA_SHIFT   9
#define ADS1115_CFG_MODE        0x0100u
#define ADS1115_CFG_DR_SHIFT    5
#define ADS1115_CFG_DR_MASK     0x00E0u
#define ADS1115_CFG_COMP_WINDOW 0x0010u
#define ADS1115_CFG_COMP_LATCH  0x0004u
#define ADS1115_CFG_COMP_MASK   0x001Fu
#define ADS1115_CFG_COMP_OFF    0x0003u
#define ADS1115_CFG_STATE_MASK  0x7FFFu

#define ADS1115_WAKE_US         25
#define ADS1115_RESET_US        50

#define SMBUS_ALERT_RESPONSE_ADDR 0x0C
#define I2C_GENERAL_CALL_ADDR     0x00
#define I2C_GENERAL_CALL_RESET    0x06
#define ARA_MAX_RESPONDERS        4

/* One 8-byte block per gain: tag, hope (BE), actual (BE), XOR, 2 reserved. */
#define EEPROM_CAL_BASE       0xD0
#define EEPROM_CAL_BLOCK_SIZE 8
#define CAL_FACTOR_MIN        0.5f
#define CAL_FACTOR_MAX        2.0f

/* Board divider (M5Stack reference driver); the inputs are wired inverted. */
#define VMETER_DIVIDER_RATIO 0.015918958f

#define VMETER_LOCK_TIMEOUT_MS 2000
#define VMETER_POINTER_UNKNOWN 0xFF

static const float FSR_MV[ UNIT_VMETER_GAIN_COUNT ] = { 6144.0f, 4096.0f, 2048.0f,
                                                        1024.0f, 512.0f,  256.0f };
static const uint16_t RATE_SPS[ UNIT_VMETER_RATE_COUNT ] = { 8,   16,  32,  64,
                                                             128, 250, 475, 860 };

typedef struct
{
  bool initialized;
  unit_vmeter_settings_t settings;
  uint16_t config; /* Expected Config register, OS bit clear */
  int16_t lo_thresh;
  int16_t hi_thresh;
  bool alert_enabled;
  unit_vmeter_alert_config_t alert;
  uint8_t alert_flags;
  float cal[ UNIT_VMETER_GAIN_COUNT ];
  uint8_t cal_mask;
  float scale_mv; /* Signed external mV per ADC code */
  uint8_t pointer;
  bool pending;
  bool result_ready;
  bool reset_detected;
  int64_t trigger_ok_us; /* An OS write before this may be ignored */
  int64_t done_nominal_us;
  int64_t done_worst_us;
  int64_t valid_us;      /* Continuous: first result with current settings */
  int64_t next_fresh_us; /* Continuous: next guaranteed-new result */
  int64_t health_due_us;
  uint32_t i2c_errors;
  uint32_t device_resets;
} vmeter_state_t;

static vmeter_state_t s_vm;
static i2c_master_dev_handle_t s_adc_dev;
static i2c_master_dev_handle_t s_eeprom_dev;
static i2c_master_dev_handle_t s_ara_dev;
#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
static bool s_pahub_held; /* one unit_pahub_init() reference */
#endif
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_wake;
static esp_timer_handle_t s_timer;

/* ---------------------------------------------------------------- timing */

static int64_t vmeter_period_us( unit_vmeter_rate_t rate )
{
  return ( 1000000 + RATE_SPS[ rate ] - 1 ) / RATE_SPS[ rate ];
}

/* Longest conversion allowed by the -10 % data-rate tolerance. */
static int64_t vmeter_worst_us( unit_vmeter_rate_t rate )
{
  int64_t div = 9 * (int64_t)RATE_SPS[ rate ];
  return ( 10000000 + div - 1 ) / div;
}

static void vmeter_timer_cb( void *arg )
{
  (void)arg;
  xSemaphoreGive( s_wake );
}

/* Sub-tick sleep: FreeRTOS ticks are 10 ms, conversions can be ~1 ms. */
static void vmeter_sleep_us( int64_t us )
{
  if( us <= 0 )
  {
    return;
  }
  xSemaphoreTake( s_wake, 0 );
  if( esp_timer_start_once( s_timer, (uint64_t)us ) == ESP_OK )
  {
    if( xSemaphoreTake( s_wake, pdMS_TO_TICKS( us / 1000 ) + 2 ) != pdTRUE )
    {
      esp_timer_stop( s_timer );
    }
    return;
  }
  vTaskDelay( pdMS_TO_TICKS( ( us + 999 ) / 1000 ) + 1 );
}

static void vmeter_wait_until( int64_t deadline_us )
{
  for( int64_t now = esp_timer_get_time(); now < deadline_us;
       now = esp_timer_get_time() )
  {
    vmeter_sleep_us( deadline_us - now );
  }
}

/* ------------------------------------------------------------------- bus */

static esp_err_t vmeter_bus_read( i2c_master_dev_handle_t dev, uint32_t reg,
                                  uint8_t *data, uint16_t length )
{
#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  return unit_pahub_i2c_read( CONFIG_UNIT_VMETER_PAHUB_CHANNEL, dev, reg, data,
                              length );
#else
  return core2foraws_expports_i2c_read( dev, reg, data, length );
#endif
}

static esp_err_t vmeter_bus_write( i2c_master_dev_handle_t dev, uint32_t reg,
                                   const uint8_t *data, uint16_t length )
{
#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  return unit_pahub_i2c_write( CONFIG_UNIT_VMETER_PAHUB_CHANNEL, dev, reg,
                               data, length );
#else
  return core2foraws_expports_i2c_write( dev, reg, data, length );
#endif
}

/* NACKs are the usual transient over cables and the isolator; timeouts
   already cost the BSP's full transfer timeout, so are not repeated. */
static bool vmeter_retryable( esp_err_t err )
{
  return err == ESP_ERR_INVALID_RESPONSE || err == ESP_FAIL;
}

static esp_err_t vmeter_read_bytes( i2c_master_dev_handle_t dev, uint32_t reg,
                                    uint8_t *data, uint16_t length )
{
  esp_err_t err;
  int attempt = 0;
  do
  {
    err = vmeter_bus_read( dev, reg, data, length );
  } while( err != ESP_OK && vmeter_retryable( err ) &&
           attempt++ < CONFIG_UNIT_VMETER_I2C_RETRIES );
  if( err != ESP_OK )
  {
    s_vm.i2c_errors++;
  }
  return err;
}

static esp_err_t vmeter_write_bytes( i2c_master_dev_handle_t dev, uint32_t reg,
                                     const uint8_t *data, uint16_t length )
{
  esp_err_t err;
  int attempt = 0;
  do
  {
    err = vmeter_bus_write( dev, reg, data, length );
  } while( err != ESP_OK && vmeter_retryable( err ) &&
           attempt++ < CONFIG_UNIT_VMETER_I2C_RETRIES );
  if( err != ESP_OK )
  {
    s_vm.i2c_errors++;
  }
  return err;
}

/* The ADS1115 keeps its pointer, so repeat reads skip the pointer byte. */
static esp_err_t vmeter_reg_read( uint8_t reg, uint16_t *value )
{
  uint8_t data[ 2 ];
  uint32_t address = s_vm.pointer == reg ? CORE2FORAWS_I2C_NO_REG : reg;
  esp_err_t err = vmeter_read_bytes( s_adc_dev, address, data, sizeof( data ) );
  if( err != ESP_OK )
  {
    s_vm.pointer = VMETER_POINTER_UNKNOWN;
    return err;
  }
  s_vm.pointer = reg;
  *value = (uint16_t)( ( data[ 0 ] << 8 ) | data[ 1 ] );
  return ESP_OK;
}

static esp_err_t vmeter_reg_write( uint8_t reg, uint16_t value )
{
  uint8_t data[ 2 ] = { (uint8_t)( value >> 8 ), (uint8_t)value };
  esp_err_t err = vmeter_write_bytes( s_adc_dev, reg, data, sizeof( data ) );
  s_vm.pointer = err == ESP_OK ? reg : VMETER_POINTER_UNKNOWN;
  return err;
}

/* ----------------------------------------------------------- calibration */

static void vmeter_load_calibration( void )
{
  uint8_t block[ UNIT_VMETER_GAIN_COUNT * EEPROM_CAL_BLOCK_SIZE ];
  s_vm.cal_mask = 0;

  esp_err_t err =
      vmeter_read_bytes( s_eeprom_dev, EEPROM_CAL_BASE, block, sizeof( block ) );
  if( err != ESP_OK )
  {
    ESP_LOGW( TAG, "Calibration EEPROM read failed: %s", esp_err_to_name( err ) );
    return;
  }

  for( int gain = 0; gain < UNIT_VMETER_GAIN_COUNT; gain++ )
  {
    const uint8_t *b = &block[ gain * EEPROM_CAL_BLOCK_SIZE ];
    uint8_t checksum = b[ 0 ] ^ b[ 1 ] ^ b[ 2 ] ^ b[ 3 ] ^ b[ 4 ];
    int16_t hope = (int16_t)( ( b[ 1 ] << 8 ) | b[ 2 ] );
    int16_t actual = (int16_t)( ( b[ 3 ] << 8 ) | b[ 4 ] );
    float factor = actual != 0 ? fabsf( (float)hope / (float)actual ) : 0.0f;

    if( checksum != b[ 5 ] || b[ 0 ] != gain || hope == 0 ||
        !( factor >= CAL_FACTOR_MIN && factor <= CAL_FACTOR_MAX ) )
    {
      ESP_LOGW( TAG, "No valid calibration for gain %d", gain );
      continue;
    }
    s_vm.cal[ gain ] = factor;
    s_vm.cal_mask |= (uint8_t)( 1u << gain );
  }
}

static float vmeter_scale_mv( unit_vmeter_gain_t gain )
{
  float factor =
      ( s_vm.cal_mask & ( 1u << gain ) ) != 0 ? s_vm.cal[ gain ] : 1.0f;
  return -( FSR_MV[ gain ] / 32768.0f ) / VMETER_DIVIDER_RATIO * factor;
}

/* ---------------------------------------------------------- config state */

static bool vmeter_settings_valid( const unit_vmeter_settings_t *settings )
{
  return settings != NULL &&
         (unsigned)settings->gain < UNIT_VMETER_GAIN_COUNT &&
         (unsigned)settings->rate < UNIT_VMETER_RATE_COUNT &&
         ( settings->mode == UNIT_VMETER_MODE_CONTINUOUS ||
           settings->mode == UNIT_VMETER_MODE_SINGLESHOT );
}

static uint16_t vmeter_config_word( const unit_vmeter_settings_t *settings,
                                    bool alert, uint8_t consecutive )
{
  /* MUX 000: AIN0-AIN1, the only inputs the board wires. */
  uint16_t config = (uint16_t)( ( settings->gain << ADS1115_CFG_PGA_SHIFT ) |
                                ( settings->mode == UNIT_VMETER_MODE_SINGLESHOT
                                      ? ADS1115_CFG_MODE
                                      : 0 ) |
                                ( settings->rate << ADS1115_CFG_DR_SHIFT ) );
  if( !alert )
  {
    return config | ADS1115_CFG_COMP_OFF;
  }
  uint16_t queue = consecutive == 1 ? 0 : consecutive == 2 ? 1 : 2;
  return config | ADS1115_CFG_COMP_WINDOW | ADS1115_CFG_COMP_LATCH | queue;
}

/* Inverted input: external V = scale * code with scale < 0, so an external
   high limit becomes the low code threshold and vice versa. */
static esp_err_t vmeter_thresholds( const unit_vmeter_alert_config_t *alert,
                                    float scale_mv, int16_t *lo, int16_t *hi )
{
  float lo_code = ceilf( alert->high_mv / scale_mv );
  float hi_code = floorf( alert->low_mv / scale_mv );
  lo_code = fminf( fmaxf( lo_code, -32768.0f ), 32767.0f );
  hi_code = fminf( fmaxf( hi_code, -32768.0f ), 32767.0f );
  if( !( hi_code > lo_code ) )
  {
    return ESP_ERR_INVALID_ARG;
  }
  *lo = (int16_t)lo_code;
  *hi = (int16_t)hi_code;
  return ESP_OK;
}

static void vmeter_timing_reset( bool was_continuous,
                                 unit_vmeter_rate_t old_rate, int64_t now )
{
  int64_t busy_until = now;
  if( was_continuous )
  {
    busy_until = now + vmeter_worst_us( old_rate );
  }
  else if( s_vm.pending && s_vm.done_worst_us > now )
  {
    busy_until = s_vm.done_worst_us;
  }
  s_vm.pending = false;
  s_vm.result_ready = false;
  s_vm.trigger_ok_us = busy_until;
  s_vm.valid_us =
      busy_until + ADS1115_WAKE_US + vmeter_worst_us( s_vm.settings.rate );
  s_vm.next_fresh_us = s_vm.valid_us;
  s_vm.health_due_us = now + CONFIG_UNIT_VMETER_HEALTH_CHECK_MS * 1000LL;
}

/* Write thresholds and Config, verify by readback, then commit the cache.
   On failure the cache keeps the old state, so the next health check
   notices any mismatch and restores it. */
static esp_err_t vmeter_apply( const unit_vmeter_settings_t *settings,
                               bool alert, int16_t lo, int16_t hi,
                               bool was_continuous, unit_vmeter_rate_t old_rate )
{
  uint16_t config = vmeter_config_word( settings, alert, s_vm.alert.consecutive );
  esp_err_t err = ESP_OK;

  if( alert )
  {
    /* Never leave the comparator armed with a half-written window. */
    if( s_vm.alert_enabled )
    {
      err = vmeter_reg_write( ADS1115_REG_CONFIG,
                              ( s_vm.config & ~ADS1115_CFG_COMP_MASK ) |
                                  ADS1115_CFG_COMP_OFF );
    }
    if( err == ESP_OK )
    {
      err = vmeter_reg_write( ADS1115_REG_LO_THRESH, (uint16_t)lo );
    }
    if( err == ESP_OK )
    {
      err = vmeter_reg_write( ADS1115_REG_HI_THRESH, (uint16_t)hi );
    }
  }
  if( err == ESP_OK )
  {
    err = vmeter_reg_write( ADS1115_REG_CONFIG, config );
  }

  int64_t now = esp_timer_get_time();
  if( err == ESP_OK )
  {
    uint16_t readback;
    err = vmeter_reg_read( ADS1115_REG_CONFIG, &readback );
    if( err == ESP_OK && ( readback & ADS1115_CFG_STATE_MASK ) != config )
    {
      ESP_LOGE( TAG, "Config readback 0x%04x, expected 0x%04x", readback,
                config );
      err = ESP_ERR_INVALID_RESPONSE;
    }
  }

  if( err == ESP_OK )
  {
    s_vm.settings = *settings;
    s_vm.config = config;
    s_vm.lo_thresh = lo;
    s_vm.hi_thresh = hi;
    s_vm.alert_enabled = alert;
    s_vm.scale_mv = vmeter_scale_mv( settings->gain );
  }
  vmeter_timing_reset( was_continuous, old_rate, now );
  return err;
}

static esp_err_t vmeter_apply_current( bool was_continuous )
{
  unit_vmeter_settings_t settings = s_vm.settings;
  bool alert = s_vm.alert_enabled;
  int16_t lo = s_vm.lo_thresh;
  int16_t hi = s_vm.hi_thresh;

  s_vm.scale_mv = vmeter_scale_mv( settings.gain );
  if( alert &&
      vmeter_thresholds( &s_vm.alert, s_vm.scale_mv, &lo, &hi ) != ESP_OK )
  {
    ESP_LOGW( TAG, "Alert window invalid for new calibration; disabled" );
    alert = false;
    s_vm.alert_enabled = false;
  }
  return vmeter_apply( &settings, alert, lo, hi, was_continuous, settings.rate );
}

/* A power glitch or hot-plug resets the ADS1115 to 0x8583; a new unit may
   also carry different calibration. */
static esp_err_t vmeter_recover( void )
{
  s_vm.device_resets++;
  s_vm.reset_detected = true;
  s_vm.pointer = VMETER_POINTER_UNKNOWN;
  ESP_LOGW( TAG, "ADS1115 reset detected; restoring configuration" );
  vmeter_load_calibration();
  esp_err_t err = vmeter_apply_current( false );
  return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
}

static esp_err_t vmeter_config_read_checked( uint16_t *config )
{
  esp_err_t err = vmeter_reg_read( ADS1115_REG_CONFIG, config );
  if( err != ESP_OK )
  {
    return err;
  }
  if( ( *config & ADS1115_CFG_STATE_MASK ) != s_vm.config )
  {
    return vmeter_recover();
  }
  s_vm.health_due_us =
      esp_timer_get_time() + CONFIG_UNIT_VMETER_HEALTH_CHECK_MS * 1000LL;
  return ESP_OK;
}

/* ---------------------------------------------------------------- alerts */

/* SMBus alert response: the lowest alerting address wins and is cleared;
   the LSB is 1 when Hi_thresh was exceeded. A NACK means nobody alerts. */
static esp_err_t vmeter_alert_collect( void )
{
  for( int i = 0; i < ARA_MAX_RESPONDERS; i++ )
  {
    uint8_t response;
    esp_err_t err = vmeter_bus_read( s_ara_dev, CORE2FORAWS_I2C_NO_REG,
                                     &response, 1 );
    if( err == ESP_ERR_INVALID_RESPONSE )
    {
      return ESP_OK;
    }
    if( err != ESP_OK )
    {
      s_vm.i2c_errors++;
      return err;
    }
    if( ( response >> 1 ) == UNIT_VMETER_ADS1115_ADDR )
    {
      s_vm.alert_flags |=
          ( response & 1 ) ? UNIT_VMETER_ALERT_LOW : UNIT_VMETER_ALERT_HIGH;
      return ESP_OK;
    }
  }
  return ESP_OK;
}

/* ----------------------------------------------------------- acquisition */

static esp_err_t vmeter_trigger( void )
{
  esp_err_t err = vmeter_reg_write( ADS1115_REG_CONFIG,
                                    s_vm.config | ADS1115_CFG_OS );
  if( err != ESP_OK )
  {
    return err;
  }
  int64_t now = esp_timer_get_time();
  s_vm.pending = true;
  s_vm.result_ready = false;
  s_vm.done_nominal_us =
      now + ADS1115_WAKE_US + vmeter_period_us( s_vm.settings.rate );
  s_vm.done_worst_us =
      now + ADS1115_WAKE_US + vmeter_worst_us( s_vm.settings.rate );
  return ESP_OK;
}

/* The trigger rewrites Config, which would hide a reset that also lost the
   thresholds, so verify first when due. */
static esp_err_t vmeter_trigger_checked( void )
{
  if( esp_timer_get_time() >= s_vm.health_due_us )
  {
    uint16_t config;
    esp_err_t err = vmeter_config_read_checked( &config );
    if( err != ESP_OK )
    {
      return err;
    }
  }
  return vmeter_trigger();
}

static int64_t vmeter_conversion_deadline( void )
{
  int64_t worst = vmeter_worst_us( s_vm.settings.rate );
  return s_vm.done_worst_us + worst / 2 + 2000;
}

/* Only called at or after done_nominal_us, so OS cannot still show the idle
   state from before the device woke up. */
static esp_err_t vmeter_poll_done( bool *done )
{
  uint16_t config;
  esp_err_t err = vmeter_config_read_checked( &config );
  if( err != ESP_OK )
  {
    return err;
  }
  *done = ( config & ADS1115_CFG_OS ) != 0;
  if( *done )
  {
    s_vm.pending = false;
    s_vm.result_ready = true;
    return ESP_OK;
  }
  if( esp_timer_get_time() > vmeter_conversion_deadline() )
  {
    ESP_LOGW( TAG, "Conversion did not complete" );
    s_vm.pending = false;
    s_vm.trigger_ok_us =
        esp_timer_get_time() + vmeter_worst_us( s_vm.settings.rate );
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

static esp_err_t vmeter_read_result( unit_vmeter_sample_t *sample )
{
  if( s_vm.alert_enabled )
  {
    /* Reading Conversion clears the latch, so collect it first. */
    esp_err_t err = vmeter_alert_collect();
    if( err != ESP_OK )
    {
      return err;
    }
  }

  uint16_t code;
  esp_err_t err = vmeter_reg_read( ADS1115_REG_CONVERSION, &code );
  if( err != ESP_OK )
  {
    return err;
  }

  int16_t raw = (int16_t)code;
  float millivolts = (float)raw * s_vm.scale_mv;
  sample->raw = raw;
  sample->millivolts = millivolts;
  sample->timestamp_us = esp_timer_get_time();
  sample->calibrated = ( s_vm.cal_mask & ( 1u << s_vm.settings.gain ) ) != 0;
  sample->over_range = raw == INT16_MAX || raw == INT16_MIN ||
                       !( fabsf( millivolts ) <= UNIT_VMETER_RATED_RANGE_MV );
  return sample->over_range ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

static esp_err_t vmeter_continuous_read( unit_vmeter_sample_t *sample )
{
  if( esp_timer_get_time() >= s_vm.health_due_us )
  {
    uint16_t config;
    esp_err_t err = vmeter_config_read_checked( &config );
    if( err != ESP_OK )
    {
      return err;
    }
  }
  esp_err_t err = vmeter_read_result( sample );
  if( err == ESP_OK || sample->over_range )
  {
    s_vm.next_fresh_us =
        sample->timestamp_us + vmeter_worst_us( s_vm.settings.rate );
  }
  return err;
}

static esp_err_t vmeter_measure_once( unit_vmeter_sample_t *sample )
{
  if( s_vm.settings.mode == UNIT_VMETER_MODE_CONTINUOUS )
  {
    vmeter_wait_until( s_vm.next_fresh_us );
    return vmeter_continuous_read( sample );
  }

  if( !s_vm.pending )
  {
    vmeter_wait_until( s_vm.trigger_ok_us );
    esp_err_t err = vmeter_trigger_checked();
    if( err != ESP_OK )
    {
      return err;
    }
  }

  vmeter_wait_until( s_vm.done_nominal_us );
  int64_t step = ( vmeter_worst_us( s_vm.settings.rate ) -
                   vmeter_period_us( s_vm.settings.rate ) ) / 4;
  if( step < 100 )
  {
    step = 100;
  }
  for( ;; )
  {
    bool done = false;
    esp_err_t err = vmeter_poll_done( &done );
    if( err != ESP_OK )
    {
      return err;
    }
    if( done )
    {
      return vmeter_read_result( sample );
    }
    vmeter_sleep_us( step );
  }
}

/* ------------------------------------------------------------- lifecycle */

static esp_err_t vmeter_lock( void )
{
  if( s_lock == NULL )
  {
    return ESP_ERR_INVALID_STATE;
  }
  return xSemaphoreTake( s_lock, pdMS_TO_TICKS( VMETER_LOCK_TIMEOUT_MS ) ) ==
                 pdTRUE
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

static void vmeter_unlock( void )
{
  xSemaphoreGive( s_lock );
}

#define VMETER_ENTER()                                                         \
  do                                                                           \
  {                                                                            \
    esp_err_t lock_err = vmeter_lock();                                        \
    if( lock_err != ESP_OK )                                                   \
      return lock_err;                                                         \
    if( !s_vm.initialized )                                                    \
    {                                                                          \
      vmeter_unlock();                                                         \
      return ESP_ERR_INVALID_STATE;                                            \
    }                                                                          \
  } while( 0 )

static esp_err_t vmeter_os_objects_create( void )
{
  if( s_lock == NULL )
  {
    s_lock = xSemaphoreCreateMutex();
  }
  if( s_wake == NULL )
  {
    s_wake = xSemaphoreCreateBinary();
  }
  if( s_lock == NULL || s_wake == NULL )
  {
    return ESP_ERR_NO_MEM;
  }
  if( s_timer == NULL )
  {
    const esp_timer_create_args_t args = { .callback = vmeter_timer_cb,
                                           .arg = NULL,
                                           .dispatch_method = ESP_TIMER_TASK,
                                           .name = "unit_vmeter",
                                           .skip_unhandled_events = true };
    return esp_timer_create( &args, &s_timer );
  }
  return ESP_OK;
}

static esp_err_t vmeter_release( i2c_master_dev_handle_t *dev )
{
  if( *dev == NULL )
  {
    return ESP_OK;
  }
  esp_err_t err = core2foraws_expports_i2c_device_remove( *dev );
  if( err == ESP_OK )
  {
    *dev = NULL;
  }
  return err;
}

static esp_err_t vmeter_release_devices( void )
{
  esp_err_t err = vmeter_release( &s_ara_dev );
  esp_err_t eeprom_err = vmeter_release( &s_eeprom_dev );
  esp_err_t adc_err = vmeter_release( &s_adc_dev );
  if( err == ESP_OK )
  {
    err = eeprom_err;
  }
  if( err == ESP_OK )
  {
    err = adc_err;
  }
#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  if( err == ESP_OK && s_pahub_held )
  {
    err = unit_pahub_deinit();
    if( err == ESP_OK )
    {
      s_pahub_held = false;
    }
  }
#endif
  return err;
}

static esp_err_t vmeter_add_devices( void )
{
  esp_err_t err = core2foraws_expports_i2c_device_add(
      UNIT_VMETER_ADS1115_ADDR, CONFIG_UNIT_VMETER_I2C_SPEED_HZ, &s_adc_dev );
  if( err == ESP_OK )
  {
    err = core2foraws_expports_i2c_device_add( UNIT_VMETER_EEPROM_ADDR,
                                               CONFIG_UNIT_VMETER_I2C_SPEED_HZ,
                                               &s_eeprom_dev );
  }
  if( err == ESP_OK )
  {
    err = core2foraws_expports_i2c_device_add( SMBUS_ALERT_RESPONSE_ADDR,
                                               CONFIG_UNIT_VMETER_I2C_SPEED_HZ,
                                               &s_ara_dev );
  }
  return err;
}

esp_err_t unit_vmeter_init( const unit_vmeter_settings_t *settings )
{
  unit_vmeter_settings_t initial =
      settings != NULL ? *settings : UNIT_VMETER_SETTINGS_DEFAULT();
  if( !vmeter_settings_valid( &initial ) )
  {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = vmeter_os_objects_create();
  if( err != ESP_OK )
  {
    return err;
  }
  err = vmeter_lock();
  if( err != ESP_OK )
  {
    return err;
  }
  if( s_vm.initialized )
  {
    vmeter_unlock();
    return ESP_ERR_INVALID_STATE;
  }

  err = vmeter_release_devices();
#ifdef CONFIG_UNIT_VMETER_USE_PAHUB
  if( err == ESP_OK )
  {
    err = unit_pahub_init();
    s_pahub_held = err == ESP_OK;
  }
#endif
  if( err == ESP_OK )
  {
    err = vmeter_add_devices();
  }

  memset( &s_vm, 0, sizeof( s_vm ) );
  s_vm.pointer = VMETER_POINTER_UNKNOWN;
  s_vm.settings = initial;
  s_vm.alert.consecutive = 1;

  /* Probe, and learn whether a previous boot left it converting. */
  uint16_t previous = 0;
  if( err == ESP_OK )
  {
    err = vmeter_reg_read( ADS1115_REG_CONFIG, &previous );
  }
  if( err == ESP_OK )
  {
    vmeter_load_calibration();
    s_vm.scale_mv = vmeter_scale_mv( initial.gain );
    bool was_continuous = ( previous & ADS1115_CFG_MODE ) == 0;
    unit_vmeter_rate_t old_rate = (unit_vmeter_rate_t)(
        ( previous & ADS1115_CFG_DR_MASK ) >> ADS1115_CFG_DR_SHIFT );
    err = vmeter_apply( &initial, false, 0, 0, was_continuous, old_rate );
  }

  if( err != ESP_OK )
  {
    ESP_LOGE( TAG, "Initialization failed: %s", esp_err_to_name( err ) );
    vmeter_release_devices();
    vmeter_unlock();
    return err;
  }

  s_vm.initialized = true;
  ESP_LOGI( TAG, "Initialized (calibration mask 0x%02x)", s_vm.cal_mask );
  vmeter_unlock();
  return ESP_OK;
}

esp_err_t unit_vmeter_deinit( void )
{
  esp_err_t err = vmeter_lock();
  if( err == ESP_ERR_INVALID_STATE )
  {
    return ESP_OK;
  }
  if( err != ESP_OK )
  {
    return err;
  }

  if( s_vm.initialized )
  {
    uint16_t config =
        ( s_vm.config & ~ADS1115_CFG_COMP_MASK ) | ADS1115_CFG_MODE |
        ADS1115_CFG_COMP_OFF;
    esp_err_t power_err = vmeter_reg_write( ADS1115_REG_CONFIG, config );
    if( power_err != ESP_OK )
    {
      ESP_LOGW( TAG, "Power-down failed: %s", esp_err_to_name( power_err ) );
    }
    s_vm.initialized = false;
  }

  err = vmeter_release_devices();
  vmeter_unlock();
  return err;
}

static esp_err_t vmeter_configure_locked( const unit_vmeter_settings_t *settings )
{
  if( !vmeter_settings_valid( settings ) )
  {
    return ESP_ERR_INVALID_ARG;
  }

  int16_t lo = s_vm.lo_thresh;
  int16_t hi = s_vm.hi_thresh;
  if( s_vm.alert_enabled &&
      vmeter_thresholds( &s_vm.alert, vmeter_scale_mv( settings->gain ), &lo,
                         &hi ) != ESP_OK )
  {
    return ESP_ERR_INVALID_ARG;
  }
  bool was_continuous = s_vm.settings.mode == UNIT_VMETER_MODE_CONTINUOUS;
  return vmeter_apply( settings, s_vm.alert_enabled, lo, hi, was_continuous,
                       s_vm.settings.rate );
}

esp_err_t unit_vmeter_configure( const unit_vmeter_settings_t *settings )
{
  VMETER_ENTER();
  esp_err_t err = vmeter_configure_locked( settings );
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_set_gain( unit_vmeter_gain_t gain )
{
  VMETER_ENTER();
  unit_vmeter_settings_t settings = s_vm.settings;
  settings.gain = gain;
  esp_err_t err = vmeter_configure_locked( &settings );
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_set_rate( unit_vmeter_rate_t rate )
{
  VMETER_ENTER();
  unit_vmeter_settings_t settings = s_vm.settings;
  settings.rate = rate;
  esp_err_t err = vmeter_configure_locked( &settings );
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_set_mode( unit_vmeter_mode_t mode )
{
  VMETER_ENTER();
  unit_vmeter_settings_t settings = s_vm.settings;
  settings.mode = mode;
  esp_err_t err = vmeter_configure_locked( &settings );
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_info_get( unit_vmeter_info_t *info )
{
  if( info == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }
  VMETER_ENTER();
  unit_vmeter_gain_t gain = s_vm.settings.gain;
  bool calibrated = ( s_vm.cal_mask & ( 1u << gain ) ) != 0;
  *info = (unit_vmeter_info_t){
    .settings = s_vm.settings,
    .full_scale_mv = FSR_MV[ gain ] / VMETER_DIVIDER_RATIO,
    .resolution_mv = fabsf( s_vm.scale_mv ),
    .calibration_factor = calibrated ? s_vm.cal[ gain ] : 1.0f,
    .calibrated = calibrated,
    .calibration_mask = s_vm.cal_mask,
    .alert_enabled = s_vm.alert_enabled,
    .i2c_errors = s_vm.i2c_errors,
    .device_resets = s_vm.device_resets,
  };
  vmeter_unlock();
  return ESP_OK;
}

esp_err_t unit_vmeter_measure( unit_vmeter_sample_t *sample )
{
  if( sample == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }
  VMETER_ENTER();
  s_vm.reset_detected = false;
  esp_err_t err = vmeter_measure_once( sample );
  if( err != ESP_OK && s_vm.reset_detected )
  {
    s_vm.reset_detected = false;
    err = vmeter_measure_once( sample );
  }
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_start_conversion( void )
{
  VMETER_ENTER();
  esp_err_t err;
  int64_t now = esp_timer_get_time();
  if( s_vm.settings.mode != UNIT_VMETER_MODE_SINGLESHOT )
  {
    err = ESP_ERR_INVALID_STATE;
  }
  else if( now < s_vm.trigger_ok_us ||
           ( s_vm.pending && now < s_vm.done_worst_us ) )
  {
    err = ESP_ERR_NOT_FINISHED;
  }
  else
  {
    err = vmeter_trigger_checked();
  }
  vmeter_unlock();
  return err;
}

static esp_err_t vmeter_ready_locked( bool *ready )
{
  *ready = false;
  if( s_vm.settings.mode == UNIT_VMETER_MODE_CONTINUOUS )
  {
    *ready = esp_timer_get_time() >= s_vm.valid_us;
    return ESP_OK;
  }
  if( s_vm.result_ready )
  {
    *ready = true;
    return ESP_OK;
  }
  if( !s_vm.pending || esp_timer_get_time() < s_vm.done_nominal_us )
  {
    return ESP_OK;
  }
  return vmeter_poll_done( ready );
}

esp_err_t unit_vmeter_conversion_ready( bool *ready )
{
  if( ready == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }
  VMETER_ENTER();
  esp_err_t err = vmeter_ready_locked( ready );
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_read( unit_vmeter_sample_t *sample )
{
  if( sample == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }
  VMETER_ENTER();
  bool ready = false;
  esp_err_t err = vmeter_ready_locked( &ready );
  if( err == ESP_OK && !ready )
  {
    err = ESP_ERR_NOT_FINISHED;
  }
  if( err == ESP_OK )
  {
    err = s_vm.settings.mode == UNIT_VMETER_MODE_CONTINUOUS
              ? vmeter_continuous_read( sample )
              : vmeter_read_result( sample );
  }
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_alert_enable( const unit_vmeter_alert_config_t *config )
{
  if( config == NULL || !isfinite( config->low_mv ) ||
      !isfinite( config->high_mv ) || !( config->low_mv < config->high_mv ) ||
      ( config->consecutive != 1 && config->consecutive != 2 &&
        config->consecutive != 4 ) )
  {
    return ESP_ERR_INVALID_ARG;
  }
  VMETER_ENTER();
  int16_t lo;
  int16_t hi;
  esp_err_t err = vmeter_thresholds( config, s_vm.scale_mv, &lo, &hi );
  if( err == ESP_OK )
  {
    unit_vmeter_alert_config_t previous = s_vm.alert;
    s_vm.alert = *config;
    unit_vmeter_settings_t settings = s_vm.settings;
    err = vmeter_apply( &settings, true, lo, hi,
                        settings.mode == UNIT_VMETER_MODE_CONTINUOUS,
                        settings.rate );
    if( err != ESP_OK )
    {
      s_vm.alert = previous;
    }
  }
  if( err == ESP_OK )
  {
    /* Drop a latch left over from earlier limits. */
    err = vmeter_alert_collect();
    s_vm.alert_flags = 0;
  }
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_alert_disable( void )
{
  VMETER_ENTER();
  esp_err_t err = ESP_OK;
  if( s_vm.alert_enabled )
  {
    unit_vmeter_settings_t settings = s_vm.settings;
    err = vmeter_apply( &settings, false, s_vm.lo_thresh, s_vm.hi_thresh,
                        settings.mode == UNIT_VMETER_MODE_CONTINUOUS,
                        settings.rate );
  }
  if( err == ESP_OK )
  {
    s_vm.alert_flags = 0;
  }
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_alert_get( uint8_t *alerts )
{
  if( alerts == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }
  VMETER_ENTER();
  esp_err_t err = s_vm.alert_enabled ? vmeter_alert_collect() : ESP_OK;
  if( err == ESP_OK )
  {
    *alerts = s_vm.alert_flags;
    s_vm.alert_flags = 0;
  }
  vmeter_unlock();
  return err;
}

esp_err_t unit_vmeter_reset( void )
{
  VMETER_ENTER();
  i2c_master_dev_handle_t general_call = NULL;
  esp_err_t err = core2foraws_expports_i2c_device_add(
      I2C_GENERAL_CALL_ADDR, CONFIG_UNIT_VMETER_I2C_SPEED_HZ, &general_call );
  if( err == ESP_OK )
  {
    const uint8_t command = I2C_GENERAL_CALL_RESET;
    err = vmeter_write_bytes( general_call, CORE2FORAWS_I2C_NO_REG, &command,
                              1 );
    esp_err_t remove_err = core2foraws_expports_i2c_device_remove( general_call );
    if( err == ESP_OK )
    {
      err = remove_err;
    }
  }
  if( err == ESP_OK )
  {
    vmeter_sleep_us( ADS1115_RESET_US );
    s_vm.pointer = VMETER_POINTER_UNKNOWN;
    vmeter_load_calibration();
    err = vmeter_apply_current( false );
  }
  vmeter_unlock();
  return err;
}
