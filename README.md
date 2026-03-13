# M5Stack VMeter Unit ESP-IDF Component for the Core2 for AWS

This is a component library for use with the M5Stack VMeter unit on the Core2 for AWS IoT Kit. Uses the abstractions built in to the [BSP for the Core2 for AWS](https://github.com/m5stack/Core2-for-AWS-IoT-Kit/tree/BSP-dev).

## Features

- Full ESP-IDF compatibility using Core2 for AWS bsp
- PA Hub multiplexer support for multi-unit connections
- Factory calibration support with EEPROM storage
- Single-shot and continuous conversion modes
- Multiple voltage ranges (±256mV to ±6.144V)
- Configurable data rates (8 to 860 samples per second)
- Input validation on all public API functions

## Configuration

### PA Hub Support

The VMeter unit can be connected directly to the Core2 expansion ports or through a PA Hub multiplexer for multi-unit configurations. Configure using menuconfig:

```text
Component config → Unit VMeter Configuration → Use PA Hub for VMeter Unit
```

When PA Hub is enabled, also configure the channel:

```text
Component config → Unit VMeter Configuration → PA Hub Channel for VMeter Unit (0-5)
```

## Usage

### Basic Single-Shot Mode

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

void app_main() {
    core2foraws_init();
    core2foraws_expports_i2c_begin();

    // Initialize in single-shot mode
    unit_vmeter_init(UNIT_VMETER_MODE_SINGLESHOT);

    float voltage;

    while( 1 )
    {
        // Start conversion
        unit_vmeter_start_conversion();

        // Wait for conversion and read result
        esp_err_t err;
        do
        {
            vTaskDelay( pdMS_TO_TICKS( 10 ) );
            err = unit_vmeter_reading_get( &voltage );
        }
        while ( err == ESP_ERR_NOT_FINISHED );

        if ( err == ESP_OK )
        {
            printf( "Voltage: %.2f mV\n", voltage );
        }

        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}
```

### Multi-Unit Configuration with PA Hub

When using multiple VMeter units through a PA Hub, configure each unit with different channels:

```c
// In menuconfig, set different channels for each VMeter unit
// Unit 1: Channel 0, Unit 2: Channel 1, etc.

#include "core2foraws.h"
#include "unit_vmeter.h"
#include "unit_pahub.h"

void app_main()
{
    core2foraws_init();
    core2foraws_expports_i2c_begin();

    // PA Hub is automatically initialized when CONFIG_UNIT_VMETER_USE_PAHUB is enabled
    unit_vmeter_init( UNIT_VMETER_MODE_CONTINUOUS );

    float voltage;

    while( 1 )
    {
        esp_err_t err = unit_vmeter_reading_get( &voltage );

        if ( err == ESP_OK )
        {
            printf( "VMeter Channel %d Voltage: %.2f mV\n",
                   CONFIG_UNIT_VMETER_PAHUB_CHANNEL, voltage );
        }

        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }
}
```

### Continuous Mode

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

void app_main()
{
    core2foraws_init();
    core2foraws_expports_i2c_begin();

    // Initialize in continuous mode
    unit_vmeter_init( UNIT_VMETER_MODE_CONTINUOUS );

    float voltage;

    while( 1 )
    {
        // Read current voltage (no start conversion needed)
        esp_err_t err = unit_vmeter_reading_get( &voltage );

        if ( err == ESP_OK )
        {
            printf( "Voltage: %.2f mV\n", voltage );
        }

        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }
}
```

### Custom Configuration

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

void app_main()
{
    core2foraws_init();
    core2foraws_expports_i2c_begin();

    // Initialize with default settings
    unit_vmeter_init( UNIT_VMETER_MODE_SINGLESHOT );

    // Configure for high voltage measurements (±4.096V range).
    // unit_vmeter_set_gain() automatically reloads EEPROM calibration for the
    // new gain — no separate unit_vmeter_load_calibration() call is needed.
    unit_vmeter_set_gain( UNIT_VMETER_GAIN_4096MV );

    // Set high sample rate
    unit_vmeter_set_rate( UNIT_VMETER_RATE_860SPS );

    float voltage;

    while( 1 )
    {
        unit_vmeter_start_conversion();

        // Wait for conversion
        esp_err_t err;
        do
        {
            vTaskDelay( pdMS_TO_TICKS( 5 ) );
            err = unit_vmeter_reading_get( &voltage );
        }
        while ( err == ESP_ERR_NOT_FINISHED );

        if ( err == ESP_OK )
        {
            printf( "High-range voltage: %.2f mV\n", voltage );
        }

        vTaskDelay( pdMS_TO_TICKS( 500 ) );
    }
}
```

## API Reference

### Initialization

- `unit_vmeter_init( mode )` - Initialize the VMeter with specified mode

### Settings

- `unit_vmeter_set_gain( gain )` - Set voltage range (also reloads EEPROM calibration)
- `unit_vmeter_set_rate( rate )` - Set sampling rate
- `unit_vmeter_set_mode( mode )` - Set operating mode

All configuration functions use read-modify-write on the ADS1115 config register
and safely clear the OS bit to avoid starting unintended conversions.

### Measurements

- `unit_vmeter_reading_get( voltage )` - Get calibrated voltage reading
- `unit_vmeter_raw_reading_get( raw_value )` - Get raw ADC value
- `unit_vmeter_is_converting()` - Check if conversion is in progress
- `unit_vmeter_start_conversion()` - Start single conversion (single-shot mode only)

### Calibration

- `unit_vmeter_load_calibration()` - Reload calibration from EEPROM for the current gain. Only needed if you want to force a re-read — `unit_vmeter_set_gain()` already calls this automatically.

## Voltage Ranges

The VMeter board has an internal voltage divider (680 kΩ / 11 kΩ, ratio ≈ 0.01592) that
scales the external voltage down before it reaches the ADS1115 ADC. The driver undoes this
scaling, so `unit_vmeter_reading_get()` always returns the **external** voltage in millivolts.

The table below shows the ADC input range for each gain setting and the resulting external
voltage resolution reported by the driver.

| Gain Setting            | ADC Input Range | Max External Voltage | External Resolution |
| ----------------------- | --------------- | -------------------- | ------------------- |
| UNIT_VMETER_GAIN_6144MV | ±6.144 V        | ±386 V               | ~11.8 mV            |
| UNIT_VMETER_GAIN_4096MV | ±4.096 V        | ±257 V               | ~7.9 mV             |
| UNIT_VMETER_GAIN_2048MV | ±2.048 V        | ±129 V               | ~3.9 mV             |
| UNIT_VMETER_GAIN_1024MV | ±1.024 V        | ±64 V                | ~2.0 mV             |
| UNIT_VMETER_GAIN_512MV  | ±0.512 V        | ±32 V ✓ recommended  | ~1.0 mV             |
| UNIT_VMETER_GAIN_256MV  | ±0.256 V        | ±16 V                | ~0.5 mV             |

**Recommended gain:** `UNIT_VMETER_GAIN_512MV` covers the module's full ±36 V measurement
range with ~1 mV resolution — matching the published board accuracy spec. Using a wider
gain range wastes ADC dynamic range and reduces resolution unnecessarily.

## Configuration Options

### Kconfig Settings

- `CONFIG_UNIT_VMETER_USE_PAHUB` - Enable PA Hub support (default: disabled)
- `CONFIG_UNIT_VMETER_PAHUB_CHANNEL` - PA Hub channel number 0-5 (default: 0)

## Notes

- The VMeter unit connects to the Core2 for AWS via I2C **Port A** (GPIO32 SDA / GPIO33 SCL). Make sure to call `core2foraws_expports_i2c_begin()` before initializing the driver.
- The VMeter unit has factory calibration stored in EEPROM at address 0x53; the driver reads it automatically on init and whenever the gain is changed. **Never write to this EEPROM** — it will corrupt the factory calibration.
- `unit_vmeter_reading_get()` returns the **external** voltage in millivolts (already compensated for the board's voltage divider). Do not apply additional divider math in your application.
- Use `UNIT_VMETER_GAIN_512MV` for general measurements up to ±36 V — it maximises ADC dynamic range and gives ~1 mV resolution.
- Use single-shot mode for battery-powered or periodic sampling to save power.
- Use continuous mode for real-time monitoring applications.
- Maximum measurement voltage: ±36 V with 1% accuracy.
- The driver is **not thread-safe** — avoid calling driver functions from multiple tasks simultaneously. If you need concurrent access, add your own mutex around driver calls.
- When using PA Hub, each VMeter unit must be configured with a unique channel number.
- PA Hub channels 0-5 are available for connecting up to 6 VMeter units simultaneously.
- The I2C bus must not exceed 1 MHz due to the CA-IS3020S isolator on the VMeter board. The Core2 for AWS BSP configures the external I2C bus at 400 kHz (fast mode), which is within this limit.
