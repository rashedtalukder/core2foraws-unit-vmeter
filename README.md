# M5Stack VMeter Unit ESP-IDF Component for the Core2 for AWS

This is a component library for use with the M5Stack VMeter unit on the Core2 for AWS IoT Kit. Uses the abstractions built in to the [BSP for the Core2 for AWS](https://github.com/m5stack/Core2-for-AWS-IoT-Kit/tree/BSP-dev).

## Features

- Full ESP-IDF compatibility using Core2 for AWS I2C abstractions
- Factory calibration support with EEPROM storage
- Single-shot and continuous conversion modes
- Multiple voltage ranges (±256mV to ±6.144V)
- Configurable data rates (8 to 860 samples per second)
- Thread-safe I2C operations

## Usage

### Basic Single-Shot Mode

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

void app_main() {
    core2foraws_init();
    
    // Initialize in single-shot mode
    unit_vmeter_init(UNIT_VMETER_MODE_SINGLESHOT);
    
    float voltage;
    
    while(1) {
        // Start conversion
        unit_vmeter_start_conversion();
        
        // Wait for conversion and read result
        esp_err_t err;
        do {
            vTaskDelay(pdMS_TO_TICKS(10));
            err = unit_vmeter_reading_get(&voltage);
        } while (err == ESP_ERR_NOT_FINISHED);
        
        if (err == ESP_OK) {
            printf("Voltage: %.2f mV\n", voltage);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Continuous Mode

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

void app_main() {
    core2foraws_init();
    
    // Initialize in continuous mode
    unit_vmeter_init(UNIT_VMETER_MODE_CONTINUOUS);
    
    float voltage;
    
    while(1) {
        // Read current voltage (no start conversion needed)
        esp_err_t err = unit_vmeter_reading_get(&voltage);
        
        if (err == ESP_OK) {
            printf("Voltage: %.2f mV\n", voltage);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### Custom Configuration

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

void app_main() {
    core2foraws_init();
    
    // Initialize with default settings
    unit_vmeter_init(UNIT_VMETER_MODE_SINGLESHOT);
    
    // Configure for high voltage measurements (±4.096V range)
    unit_vmeter_set_gain(UNIT_VMETER_GAIN_4096MV);
    
    // Set high sample rate
    unit_vmeter_set_rate(UNIT_VMETER_RATE_860SPS);
    
    // Reload calibration for new gain setting
    unit_vmeter_load_calibration();
    
    float voltage;
    
    while(1) {
        unit_vmeter_start_conversion();
        
        // Wait for conversion
        esp_err_t err;
        do {
            vTaskDelay(pdMS_TO_TICKS(5));
            err = unit_vmeter_reading_get(&voltage);
        } while (err == ESP_ERR_NOT_FINISHED);
        
        if (err == ESP_OK) {
            printf("High-range voltage: %.2f mV\n", voltage);
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

## API Reference

### Initialization
- `unit_vmeter_init(mode)` - Initialize the VMeter with specified mode

### Configuration
- `unit_vmeter_set_gain(gain)` - Set voltage range
- `unit_vmeter_set_rate(rate)` - Set sampling rate
- `unit_vmeter_set_mode(mode)` - Set operating mode

### Measurements
- `unit_vmeter_reading_get(voltage)` - Get calibrated voltage reading
- `unit_vmeter_raw_reading_get(raw_value)` - Get raw ADC value
- `unit_vmeter_is_converting()` - Check if conversion is in progress
- `unit_vmeter_start_conversion()` - Start single conversion (single-shot mode only)

### Calibration
- `unit_vmeter_load_calibration()` - Reload calibration from EEPROM

## Voltage Ranges

| Gain Setting | Voltage Range | Resolution |
|--------------|---------------|------------|
| UNIT_VMETER_GAIN_6144MV | ±6.144V | 0.1875 mV |
| UNIT_VMETER_GAIN_4096MV | ±4.096V | 0.125 mV |
| UNIT_VMETER_GAIN_2048MV | ±2.048V | 0.0625 mV |
| UNIT_VMETER_GAIN_1024MV | ±1.024V | 0.03125 mV |
| UNIT_VMETER_GAIN_512MV  | ±0.512V | 0.015625 mV |
| UNIT_VMETER_GAIN_256MV  | ±0.256V | 0.007813 mV |

## Notes

- The VMeter unit has factory calibration stored in EEPROM at address 0x53
- The component automatically reads and applies calibration factors during initialization
- Use single-shot mode for battery-powered applications to save power
- Use continuous mode for real-time monitoring applications
- Maximum measurement voltage: ±36V with 1% accuracy
- I2C communication is electrically isolated for noise immunity
