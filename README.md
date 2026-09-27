# M5Stack Unit VMeter ESP-IDF Component

Driver for the isolated [M5Stack Unit VMeter](https://docs.m5stack.com/en/unit/vmeter) (U087) on the Core2 for AWS IoT Kit BSP. The board has an ADS1115 at `0x49` and a factory calibration EEPROM at `0x53`. The driver only reads the EEPROM and never writes it.

M5Stack rates the module for `+/-36 V`, with 1% full-scale accuracy plus one digit. The PGA setting selects the ADC input range. It does not make higher external voltages safe.

## Features

- All six PGA gains, all eight data rates, and both single-shot and continuous modes. Each configuration change is a single register write, verified by reading it back.
- Every factory calibration block is loaded once at init and validated (checksum, gain tag, plausible factor). Gain changes therefore need no EEPROM traffic.
- `unit_vmeter_measure()` always returns a new conversion:
  - It sleeps for the conversion time with microsecond resolution (`esp_timer`), not in 10 ms ticks.
  - Waits allow for the ADS1115's ±10% data-rate tolerance and the 25 µs single-shot wake-up.
  - In continuous mode it waits for a sample newer than the previous read. Repeat reads skip the pointer byte.
- The Config register is checked on every single-shot conversion, and periodically in continuous mode. If a unit reset or hot-plug is detected, the driver restores configuration, thresholds and calibration automatically.
- Transfers that NACK are retried. The driver tracks error and reset counters, available from `unit_vmeter_info_get()`.
- Hardware window-comparator alerts in external millivolts, with 1/2/4-conversion qualification. ALERT/RDY is not wired to the connector, so latched events are collected over I2C with the SMBus alert response (`0x0C`). No event is lost when a read clears the latch.
- I2C general-call reset with full state restore.
- Thread-safe. Direct Port A or PaHub routing.

AIN0-AIN1 is the only input pair wired on the board. Conversion-ready signalling needs the ALERT/RDY pin, so it is not available.

## Usage

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

core2foraws_init();
ESP_ERROR_CHECK( core2foraws_expports_i2c_begin() );

unit_vmeter_settings_t settings = UNIT_VMETER_SETTINGS_DEFAULT();
settings.gain = UNIT_VMETER_GAIN_512MV; /* +/-32 V external, ~1 mV/LSB */
ESP_ERROR_CHECK( unit_vmeter_init( &settings ) );

unit_vmeter_sample_t sample;
esp_err_t err = unit_vmeter_measure( &sample );
if( err == ESP_OK )
{
  printf( "%.1f mV%s\n", sample.millivolts, sample.calibrated ? "" : " (uncalibrated)" );
}
```

`ESP_ERR_INVALID_RESPONSE` means there is no trustworthy value. Either the ADC clipped or the reading is outside `+/-36 V` (`sample.over_range` is set and the sample is still filled for diagnostics), or the unit kept resetting.

Undervoltage/overvoltage watch, with the chip checking every conversion:

```c
unit_vmeter_set_mode( UNIT_VMETER_MODE_CONTINUOUS );
unit_vmeter_alert_enable( &(unit_vmeter_alert_config_t){ .low_mv = 11500, .high_mv = 14600, .consecutive = 2 } );

uint8_t alerts;
if( unit_vmeter_alert_get( &alerts ) == ESP_OK && ( alerts & UNIT_VMETER_ALERT_LOW ) )
{
  /* The input dropped below 11.5 V at some point since the last check. */
}
```

The SMBus alert response also clears latched alerts of other devices on the same bus segment. `unit_vmeter_reset()` resets every general-call-capable device on that segment.

## Configuration

| Option | Default | Purpose |
| --- | --- | --- |
| `CONFIG_UNIT_VMETER_USE_PAHUB` / `_PAHUB_CHANNEL` | off / 0 | Route all transfers through a PaHub channel |
| `CONFIG_UNIT_VMETER_I2C_SPEED_HZ` | 400000 | SCL rate. The isolator limit is 1 MHz |
| `CONFIG_UNIT_VMETER_I2C_RETRIES` | 2 | Retries after a NACK |
| `CONFIG_UNIT_VMETER_HEALTH_CHECK_MS` | 1000 | How often a continuous-mode read verifies the Config register |

See [datasheets/vmeter.md](datasheets/vmeter.md) for board details and the checked-in ADS1115 PDF for chip-level behaviour.
