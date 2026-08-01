# M5Stack Unit VMeter ESP-IDF Component

Driver for the isolated [M5Stack Unit VMeter](https://docs.m5stack.com/en/unit/vmeter) (U087). The board contains an ADS1115 at `0x49` and factory calibration EEPROM at `0x53`; never write to the EEPROM.

M5Stack rates the module for `+/-36 V`, with 1% full-scale accuracy plus one digit. PGA settings describe the ADS1115 input range, not a higher safe board voltage.

## Usage

```c
#include "core2foraws.h"
#include "unit_vmeter.h"

core2foraws_init();
ESP_ERROR_CHECK( core2foraws_expports_i2c_begin() );
ESP_ERROR_CHECK( unit_vmeter_init( UNIT_VMETER_MODE_SINGLESHOT ) );

/* Official M5Stack examples use this gain for measurements up to 16 V. */
ESP_ERROR_CHECK( unit_vmeter_set_gain( UNIT_VMETER_GAIN_512MV ) );
ESP_ERROR_CHECK( unit_vmeter_start_conversion() );

while( unit_vmeter_is_converting() )
{
  vTaskDelay( pdMS_TO_TICKS( 2 ) );
}

float millivolts = 0.0f;
ESP_ERROR_CHECK( unit_vmeter_reading_get( &millivolts ) );
```

`unit_vmeter_reading_get()` returns external terminal voltage in millivolts after divider and EEPROM gain calibration. Changing gain reloads the matching calibration block and returns an error if it is invalid. Conversion status is conservative: I2C errors report “still converting,” allowing callers to time out instead of consuming stale data.

Enable `CONFIG_UNIT_VMETER_USE_PAHUB` to route the ADS1115 and EEPROM through a selected PaHub channel.

See [datasheets/vmeter.md](datasheets/vmeter.md) for board details and the checked-in ADS1115 PDF for chip-level behavior.
