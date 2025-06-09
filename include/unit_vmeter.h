#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

// Device I2C addresses
#define UNIT_VMETER_ADS1115_ADDR    0x49
#define UNIT_VMETER_EEPROM_ADDR     0x53

// ADS1115 register addresses
#define ADS1115_REG_CONVERSION      0x00
#define ADS1115_REG_CONFIG          0x01

// Gain settings (PGA)
typedef enum {
    UNIT_VMETER_GAIN_6144MV = 0x00,    // ±6.144V range
    UNIT_VMETER_GAIN_4096MV = 0x01,    // ±4.096V range
    UNIT_VMETER_GAIN_2048MV = 0x02,    // ±2.048V range (default)
    UNIT_VMETER_GAIN_1024MV = 0x03,    // ±1.024V range
    UNIT_VMETER_GAIN_512MV  = 0x04,    // ±0.512V range
    UNIT_VMETER_GAIN_256MV  = 0x05     // ±0.256V range
} unit_vmeter_gain_t;

// Data rate settings
typedef enum {
    UNIT_VMETER_RATE_8SPS   = 0x00,    // 8 samples per second
    UNIT_VMETER_RATE_16SPS  = 0x01,    // 16 samples per second
    UNIT_VMETER_RATE_32SPS  = 0x02,    // 32 samples per second
    UNIT_VMETER_RATE_64SPS  = 0x03,    // 64 samples per second
    UNIT_VMETER_RATE_128SPS = 0x04,    // 128 samples per second (default)
    UNIT_VMETER_RATE_250SPS = 0x05,    // 250 samples per second
    UNIT_VMETER_RATE_475SPS = 0x06,    // 475 samples per second
    UNIT_VMETER_RATE_860SPS = 0x07     // 860 samples per second
} unit_vmeter_rate_t;

// Operating mode
typedef enum {
    UNIT_VMETER_MODE_CONTINUOUS = 0x00, // Continuous conversion mode
    UNIT_VMETER_MODE_SINGLESHOT = 0x01  // Single-shot conversion mode
} unit_vmeter_mode_t;

// Configuration structure
typedef struct {
    unit_vmeter_gain_t gain;
    unit_vmeter_rate_t rate;
    unit_vmeter_mode_t mode;
    float calibration_factor;
    bool calibration_loaded;
} unit_vmeter_config_t;

/**
 * @brief Initialize the VMeter unit
 * 
 * @param mode Operating mode (SINGLESHOT or CONTINUOUS)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t unit_vmeter_init(unit_vmeter_mode_t mode);

/**
 * @brief Set the gain (voltage range) for measurements
 * 
 * @param gain Gain setting
 * @return esp_err_t ESP_OK on success
 */
esp_err_t unit_vmeter_set_gain(unit_vmeter_gain_t gain);

/**
 * @brief Set the data rate for measurements
 * 
 * @param rate Data rate setting
 * @return esp_err_t ESP_OK on success
 */
esp_err_t unit_vmeter_set_rate(unit_vmeter_rate_t rate);

/**
 * @brief Set the operating mode
 * 
 * @param mode Operating mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t unit_vmeter_set_mode(unit_vmeter_mode_t mode);

/**
 * @brief Get voltage reading
 * 
 * @param voltage Pointer to store voltage reading in millivolts
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FINISHED if conversion not ready
 */
esp_err_t unit_vmeter_reading_get(float *voltage);

/**
 * @brief Get raw ADC reading
 * 
 * @param raw_value Pointer to store raw ADC value
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FINISHED if conversion not ready
 */
esp_err_t unit_vmeter_raw_reading_get(int16_t *raw_value);

/**
 * @brief Check if conversion is in progress
 * 
 * @return true if conversion is in progress, false if ready
 */
bool unit_vmeter_is_converting(void);

/**
 * @brief Start a single conversion (only applicable in single-shot mode)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t unit_vmeter_start_conversion(void);

/**
 * @brief Load calibration data from EEPROM
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t unit_vmeter_load_calibration(void);

#ifdef __cplusplus
}
#endif
