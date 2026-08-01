# Unit VMeter Firmware Driver Implementation Specification

## Source Evidence and Precedence

Verified on 2026-08-01 against the official [M5Stack Unit VMeter page](https://docs.m5stack.com/en/unit/vmeter), board schematic, checked-in TI `ADS1115-datasheet.pdf` (SHA-256 `62661b1409e83995a9f696305aa9497c4c41bfde6cbc7e920d5b3e263a1cc0cc`), and official [M5Stack M5-ADS1115 reference driver](https://github.com/m5stack/M5-ADS1115) commit `129349489310a96feed15f9d922081aa641c62f9`. The M5Stack sources control board range, divider, sign, and EEPROM format; TI controls ADS1115 behavior. See `schema.yml`.

## 1. Purpose

This document is a self-contained firmware implementation reference for the M5Stack Unit VMeter module. It is intended to enable automatic generation of firmware drivers, HAL layers, BSP support, I2C communication code, and automated tests without requiring the original source documents.

This specification covers the complete software-visible behavior of the Unit VMeter as a board-level device built around:

* Texas Instruments ADS1115 16-bit I2C ADC at I2C address `0x49`  
* Chipanalog CA-IS3020S isolated bidirectional I2C isolator  
* Board EEPROM at I2C address `0x53` containing factory calibration data; this EEPROM must not be written by firmware because calibration would be overwritten 

The board calibration format below is implementation evidence from M5Stack's reference driver; it is not part of the ADS1115 datasheet.

---

## 2. Device Overview

The Unit VMeter is an isolated voltage measurement module for real-time monitoring of external voltage up to `±36 V`. The module uses an ADS1115 16-bit ADC, an isolated I2C interface, an isolated DC-DC supply, and factory-programmed calibration data stored in EEPROM. Published board-level performance is:

* Measurement range: `±36 V`
* Resolution: `1 mV at ≤16 V`, `7.9 mV above 16 V`
* Accuracy: `1% of full scale, ±1 digit`
* ADC address: `0x49`
* Calibration EEPROM address: `0x53`
* Isolation withstand voltage: up to `1000 VRMS` for the module
* PORT.A pinout: `GND`, `5V`, `SDA`, `SCL` 

Relevant internal subsystems:

* ADS1115 ADC core with:

  * I2C interface
  * 4-channel mux / 2 differential channels
  * PGA
  * single-shot and continuous modes
  * comparator / conversion-ready logic 
* CA-IS3020S bidirectional isolated I2C bridge for SDA and SCL  
* Board EEPROM storing factory calibration constants at `0x53` 
* Isolated analog front end using resistor network shown in the published VMeter schematic image:

  * `11 kΩ`
  * `680 kΩ`
  * `2.5 V` bias network
  * ADS1115 channel use shown as `AIN0` and `AIN1`

M5Stack's reference driver confirms a divider coefficient of `0.015918958`, negative measurement direction, and one eight-byte calibration block per PGA setting beginning at EEPROM offset `0xD0 + gain * 8`.

---

## 3. Communication Interfaces

## 3.1 External Host Interface

The module exposes a single I2C host interface on HY2.0-4P PORT.A:

| Pin    | Signal |
| ------ | ------ |
| Black  | GND    |
| Red    | 5V     |
| Yellow | SDA    |
| White  | SCL    |

Published module host-facing I2C devices:

* ADS1115 ADC at `0x49`
* EEPROM at `0x53` 

## 3.2 ADC Interface: ADS1115

ADS1115 properties relevant to firmware:

* Interface: I2C-compatible
* Supply range: `2.0 V to 5.5 V`
* Sample rates: `8, 16, 32, 64, 128, 250, 475, 860 SPS`
* Resolution: `16 bits`
* Modes: continuous conversion, single-shot
* Inputs: `4` single-ended or `2` differential
* PGA full-scale ranges: `±0.256 V` to `±6.144 V`
* I2C speed:

  * fast mode: `0.01 MHz to 0.4 MHz`
  * high-speed mode: `0.01 MHz to 3.4 MHz`
* Device never drives SCL   

## 3.3 Isolator Interface: CA-IS3020S

The CA-IS3020S is not an addressable peripheral. It transparently isolates SDA and SCL.

Published isolator properties:

* Two bidirectional I2C channels
* Open-drain outputs
* Signal transfer rate: `DC to 1 MHz`
* Supply range on each side: `3 V to 5.5 V`
* Operating temperature: `-55°C to 125°C`
* Side A low-level sink capability: `3.5 mA`
* Side B low-level sink capability: `35 mA`
* A-side max load capacitance: `40 pF`
* B-side max load capacitance: `400 pF`
* Spike suppression filter `tSP`: `10 ns min`, `25 ns typ`
* CMTI: `±150 kV/µs`
* UL1577 isolation rating: `5000 VRMS` device-level
* VIORM max repetitive peak isolation voltage:

  * `849 VPK` wide-body G
  * `565 VPK` narrow-body S
* VIOSM max surge isolation voltage:

  * `6250 VPK` wide-body G
  * `5000 VPK` narrow-body S  

Because the board description calls the installed part `CA-IS3020S`, the narrow-body SOIC8 package data apply:

* package type `SOIC8`
* rated isolation `5.0 kVRMS` device-level 

---

## 4. Device Addressing and Identification

## 4.1 VMeter Board-Level Address Map

| Device  | I2C Address | Function                    |
| ------- | ----------: | --------------------------- |
| ADS1115 |      `0x49` | ADC conversion engine       |
| EEPROM  |      `0x53` | Factory calibration storage |

The module documentation explicitly warns not to write the EEPROM at `0x53` because calibration data will be overwritten and measurements will become inaccurate. 

## 4.2 ADS1115 Address Selection

ADS1115 supports four pin-selectable addresses via ADDR pin. The quickstart example shows a device with 7-bit address `1001000b` = `0x48`. The VMeter board uses `0x49`, therefore the ADDR pin is strapped for that address on this board. 

For this board, firmware must use `0x49` and not attempt dynamic address discovery unless explicitly scanning the bus.

## 4.3 Identification Registers

* ADS1115: no dedicated manufacturer ID or revision ID register is documented in the provided data.
* CA-IS3020S: not software-addressable.
* EEPROM: exact contents and identification bytes are not published in the provided materials.

## 4.4 Device Discovery Procedure

Recommended board discovery:

1. Probe I2C address `0x49`.
2. Probe I2C address `0x53`.
3. Read ADS1115 config register and verify default/reset-compatible behavior.
4. Do not write EEPROM.
5. Treat absence of EEPROM as degraded mode only if explicitly supported by application policy.

---

## 5. Communication Protocol

## 5.1 ADS1115 Register Access Model

ADS1115 has four data registers accessed through a separate Address Pointer register. Pointer values:

* `00b` → Conversion register
* `01b` → Config register
* `10b` → Lo_thresh register
* `11b` → Hi_thresh register

Address Pointer register format:

| Bits | Name     | Access | Reset | Description       |
| ---- | -------- | ------ | ----- | ----------------- |
| 7:2  | Reserved | W      | `0h`  | Always write `0h` |
| 1:0  | P[1:0]   | W      | `0h`  | Register select   |

All ADS1115 register reads and writes are preceded by START and followed by STOP. Pointer value persists until changed.  

## 5.2 ADS1115 Write Transaction

To write a 16-bit target register:

1. START
2. Slave address + write
3. Pointer byte
4. Register MSB
5. Register LSB
6. STOP

## 5.3 ADS1115 Read Transaction

To read a 16-bit target register:

1. START
2. Slave address + write
3. Pointer byte
4. STOP or repeated START
5. START
6. Slave address + read
7. Read MSB
8. Read LSB
9. STOP

Repeated reads of the same register can omit rewriting the pointer. 

## 5.4 Example ADS1115 Transactions

Example from the datasheet for address `0x48`:

* write config:

  * address byte write: `0b10010000`
  * pointer: `0b00000001`
  * config MSB: `0b10000100`
  * config LSB: `0b10000011`
* write pointer to conversion register:

  * address byte write: `0b10010000`
  * pointer: `0b00000000`
* read conversion:

  * address byte read: `0b10010001`
  * read MSB, then LSB 

For VMeter address `0x49`, use:

* write byte address: `(0x49 << 1) | 0 = 0x92`
* read byte address: `(0x49 << 1) | 1 = 0x93`

## 5.5 EEPROM Access

M5Stack's reference driver reads one eight-byte block at `0xD0 + gain * 8`:

| Byte | Meaning |
|---:|---|
| 0 | PGA gain tag (`0..5`) |
| 1..2 | signed big-endian expected (`hope`) code |
| 3..4 | signed big-endian measured (`actual`) code |
| 5 | XOR of bytes 0..4 |
| 6..7 | reserved by the reference format |

The gain factor is `abs(hope / actual)`. Reject checksum, gain-tag, or zero-value failures. The official product warning remains controlling: **never write this EEPROM in normal firmware**.

---

## 6. Register Map

## 6.1 ADS1115 Register Map

| Register   | Pointer Address | Access |    Reset | Description               |
| ---------- | --------------: | ------ | -------: | ------------------------- |
| Conversion |          `0x00` | R      | `0x0000` | Last conversion result    |
| Config     |          `0x01` | R/W    | `0x8583` | Configuration and status  |
| Lo_thresh  |          `0x02` | R/W    | `0x8000` | Comparator low threshold  |
| Hi_thresh  |          `0x03` | R/W    | `0x7FFF` | Comparator high threshold |

  

## 6.2 CA-IS3020S Register Map

None. This device is not register-programmed. It is a transparent hardware isolator.

## 6.3 EEPROM Register Map

See section 5.5 for the read-only board calibration block layout corroborated by M5Stack's reference driver.

---

## 7. Register Bitfields

## 7.1 Address Pointer Register

| Bits | Name     | Access | Reset | Description                                                  |
| ---- | -------- | ------ | ----- | ------------------------------------------------------------ |
| 7:2  | Reserved | W      | `0h`  | Must be written `0`                                          |
| 1:0  | P[1:0]   | W      | `0h`  | `00` Conversion, `01` Config, `10` Lo_thresh, `11` Hi_thresh |



## 7.2 Conversion Register (`0x00`)

| Bits | Name    | Access | Reset    | Description                                         |
| ---- | ------- | ------ | -------- | --------------------------------------------------- |
| 15:0 | D[15:0] | R      | `0x0000` | 16-bit conversion result in binary two's complement |

Following power-up, this register remains `0x0000` until the first conversion completes. 

## 7.3 Config Register (`0x01`)

Reset value: `0x8583`

Bit layout:

| Bits  | Name          | Access | Reset | Description                                 |
| ----- | ------------- | ------ | ----- | ------------------------------------------- |
| 15    | OS            | R/W    | `1`   | Start single conversion / conversion status |
| 14:12 | MUX[2:0]      | R/W    | `000` | Input multiplexer selection                 |
| 11:9  | PGA[2:0]      | R/W    | `010` | Full-scale range                            |
| 8     | MODE          | R/W    | `1`   | Device mode                                 |
| 7:5   | DR[2:0]       | R/W    | `100` | Data rate                                   |
| 4     | COMP_MODE     | R/W    | `0`   | Comparator mode                             |
| 3     | COMP_POL      | R/W    | `0`   | ALERT/RDY polarity                          |
| 2     | COMP_LAT      | R/W    | `0`   | Latching comparator                         |
| 1:0   | COMP_QUE[1:0] | R/W    | `11`  | Comparator queue / disable                  |



### 7.3.1 OS

|     Value | Meaning                                    |
| --------: | ------------------------------------------ |
| write `0` | No effect                                  |
| write `1` | Start single conversion when in power-down |
|  read `0` | Conversion in progress                     |
|  read `1` | Device idle / not converting               |



### 7.3.2 MUX[2:0]

| Value | Meaning                  |
| ----: | ------------------------ |
| `000` | AINP = AIN0, AINN = AIN1 |
| `001` | AINP = AIN0, AINN = AIN3 |
| `010` | AINP = AIN1, AINN = AIN3 |
| `011` | AINP = AIN2, AINN = AIN3 |
| `100` | AINP = AIN0, AINN = GND  |
| `101` | AINP = AIN1, AINN = GND  |
| `110` | AINP = AIN2, AINN = GND  |
| `111` | AINP = AIN3, AINN = GND  |

The published VMeter schematic shows analog usage of `AIN0` and `AIN1`, so board firmware should assume the board front end is wired around those channels unless proven otherwise. 

### 7.3.3 PGA[2:0]

| Value | FSR                |
| ----: | ------------------ |
| `000` | `±6.144 V`         |
| `001` | `±4.096 V`         |
| `010` | `±2.048 V` default |
| `011` | `±1.024 V`         |
| `100` | `±0.512 V`         |
| `101` | `±0.256 V`         |
| `110` | `±0.256 V`         |
| `111` | `±0.256 V`         |

Do not apply more than `VDD + 0.3 V` to analog inputs. 

### 7.3.4 MODE

| Value | Meaning                                           |
| ----: | ------------------------------------------------- |
|   `0` | Continuous-conversion mode                        |
|   `1` | Single-shot mode / power-down between conversions |

The ADS111x automatically power down after one conversion in single-shot mode. 

### 7.3.5 DR[2:0]

| Value | Data Rate         |
| ----: | ----------------- |
| `000` | `8 SPS`           |
| `001` | `16 SPS`          |
| `010` | `32 SPS`          |
| `011` | `64 SPS`          |
| `100` | `128 SPS` default |
| `101` | `250 SPS`         |
| `110` | `475 SPS`         |
| `111` | `860 SPS`         |

Conversion time equals `1 / DR`. 

### 7.3.6 COMP_MODE

| Value | Meaning                |
| ----: | ---------------------- |
|   `0` | Traditional comparator |
|   `1` | Window comparator      |



### 7.3.7 COMP_POL

| Value | Meaning            |
| ----: | ------------------ |
|   `0` | Active low default |
|   `1` | Active high        |



### 7.3.8 COMP_LAT

| Value | Meaning                                                |
| ----: | ------------------------------------------------------ |
|   `0` | Nonlatching default                                    |
|   `1` | Latching until conversion read or SMBus alert response |



### 7.3.9 COMP_QUE[1:0]

| Value | Meaning                                              |
| ----: | ---------------------------------------------------- |
|  `00` | Assert after 1 conversion                            |
|  `01` | Assert after 2 conversions                           |
|  `10` | Assert after 4 conversions                           |
|  `11` | Disable comparator, ALERT/RDY high impedance default |



## 7.4 Lo_thresh Register (`0x02`)

Reset value: `0x8000`

| Bits | Name       | Access | Reset    | Description                                |
| ---- | ---------- | ------ | -------- | ------------------------------------------ |
| 15:0 | TH_L[15:0] | R/W    | `0x8000` | Lower threshold in two's complement format |

## 7.5 Hi_thresh Register (`0x03`)

Reset value: `0x7FFF`

| Bits | Name       | Access | Reset    | Description                                |
| ---- | ---------- | ------ | -------- | ------------------------------------------ |
| 15:0 | TH_H[15:0] | R/W    | `0x7FFF` | Upper threshold in two's complement format |

Thresholds are digital comparator values and must be updated if PGA changes. 

---

## 8. Reserved Bit Handling

### ADS1115 Address Pointer Register

* Bits `7:2` must always be written `0`.

### ADS1115 Other Registers

* No undocumented reserved bits are present in the documented register set.
* For any future-compatible implementation, preserve register fields not intentionally modified by performing read-modify-write at the software abstraction level.

### EEPROM

* Unknown.
* Do not write any EEPROM locations.

### CA-IS3020S

* No registers.

---

## 9. Commands or Opcodes

## 9.1 ADS1115

No standalone opcode set beyond normal I2C addressing and register/pointer accesses.

## 9.2 SMBus Alert Command

In latching comparator mode, host may issue SMBus alert command `00011001` when ALERT/RDY is asserted; responding ADS1114/ADS1115 devices return their slave address. 

## 9.3 I2C General-Call Reset

The ADS1115 responds to the I2C general call: address byte `0x00` (`0000000` + write bit) followed by data byte `0x06` (`00000110b`) triggers an internal reset, restoring all Config bits to defaults and entering power-down. This affects all reset-capable devices on the bus.

## 9.4 EEPROM

No command set is documented in the supplied materials.

---

## 10. Data Formats

## 10.1 ADS1115 Conversion Data

* Width: `16 bits`
* Encoding: binary two's complement
* Byte order on I2C: MSB first, then LSB
* Positive full scale output: `0x7FFF`
* Zero input: `0x0000`
* Negative 1 LSB: `0xFFFF`
* Negative full scale output: `0x8000`

Ideal code relationship:

| Input Signal `VIN = VAINP - VAINN` | Ideal Output |
| ---------------------------------- | -----------: |
| `≥ +FS * (2^15 - 1)/2^15`          |     `0x7FFF` |
| `+FS/2^15`                         |     `0x0001` |
| `0`                                |     `0x0000` |
| `-FS/2^15`                         |     `0xFFFF` |
| `≤ -FS`                            |     `0x8000` |

Single-ended measurements only use positive code range `0x0000` to `0x7FFF`, though offsets can still produce small negative codes near zero. 

## 10.2 LSB Size by PGA Range

For ADS1115, LSB size is:

`LSB = FSR / 32768`

| PGA Setting   |        FSR |    LSB Size |
| ------------- | ---------: | ----------: |
| `000`         | `±6.144 V` |  `187.5 µV` |
| `001`         | `±4.096 V` |    `125 µV` |
| `010`         | `±2.048 V` |   `62.5 µV` |
| `011`         | `±1.024 V` |  `31.25 µV` |
| `100`         | `±0.512 V` | `15.625 µV` |
| `101/110/111` | `±0.256 V` | `7.8125 µV` |

Derived from documented FSR values. 

## 10.3 VMeter Board-Level Voltage Data

Published board-level result resolution:

* `1 mV` at readings `≤16 V`
* `7.9 mV` above `16 V`

Published board-level full measurement range:

* `±36 V`

Published board-level accuracy:

* `1% of full scale, ±1 digit` 

### Board conversion

The official reference implementation uses:

```text
external_mV = raw_code * ADS1115_mV_per_LSB / 0.015918958
              * abs(hope / actual) * -1
```

The `-1` corrects the board's differential measurement direction. This formula does not extend the product rating: clamp or reject values outside the published `±36 V` module range.

---

## 11. Timing Requirements

## 11.1 ADS1115 I2C Timing

At `VDD = 2.0 V to 5.5 V`:

| Parameter | Fast Mode Min | Fast Mode Max | High-Speed Min | High-Speed Max | Unit |
| --------- | ------------: | ------------: | -------------: | -------------: | ---- |
| `fSCL`    |        `0.01` |         `0.4` |         `0.01` |          `3.4` | MHz  |
| `tBUF`    |         `600` |             — |          `160` |              — | ns   |
| `tHDSTA`  |         `600` |             — |          `160` |              — | ns   |
| `tSUSTA`  |         `600` |             — |          `160` |              — | ns   |
| `tSUSTO`  |         `600` |             — |          `160` |              — | ns   |
| `tHDDAT`  |           `0` |             — |            `0` |              — | ns   |
| `tSUDAT`  |         `100` |             — |           `10` |              — | ns   |
| `tLOW`    |        `1300` |             — |          `160` |              — | ns   |
| `tHIGH`   |         `600` |             — |           `60` |              — | ns   |
| `tF`      |             — |         `300` |              — |          `160` | ns   |
| `tR`      |             — |         `300` |              — |          `160` | ns   |

For high-speed mode maximum values, bus capacitance must not exceed `400 pF`. 

## 11.2 ADS1115 Conversion Timing

Data rate settings and conversion time:

|   SPS |   Conversion Time |
| ----: | ----------------: |
|   `8` |          `125 ms` |
|  `16` |         `62.5 ms` |
|  `32` |        `31.25 ms` |
|  `64` |       `15.625 ms` |
| `128` |       `7.8125 ms` |
| `250` |            `4 ms` |
| `475` |  `2.105263... ms` |
| `860` | `1.1627907... ms` |

The ADS111x settle within a single cycle, so conversion time equals `1 / DR`. 

## 11.3 ADS1115 Supply and Startup-Relevant Timing

* Internal oscillator: `1 MHz`
* No external clock
* Following power-up, conversion register remains `0x0000` until first conversion completes
* Wait approximately `50 µs` after VDD is stable before communicating, to allow the power-up reset process to complete
* In single-shot mode, after `OS = 1` is written the device powers up in approximately `25 µs`, clears `OS` to `0`, then starts the conversion
* If the I2C bus is held idle for more than `25 ms`, the bus times out  

## 11.4 CA-IS3020S Timing

* Signal transfer rate: `DC to 1 MHz`
* Input spike filter `tSP`: `10 ns min`, `25 ns typ`

No additional software-programmable timing exists. Host I2C bus speed must not exceed the isolator path capability of `1 MHz`, even though ADS1115 itself supports up to `3.4 MHz`. Therefore, for this board the effective safe upper bound is `1 MHz`.  

---

## 12. Operating Modes

## 12.1 ADS1115 Modes

### Single-shot mode

* Config `MODE = 1`
* Device powers down between conversions
* Writing `OS = 1` starts one conversion
* Read `OS` until it returns `1`, or wait `1 / DR` plus margin

### Continuous-conversion mode

* Config `MODE = 0`
* ADC continuously converts at configured data rate
* Conversion register always contains most recent completed sample

## 12.2 Comparator Modes

### Traditional comparator

ALERT asserts when conversion exceeds `Hi_thresh`; deasserts when conversion falls below `Lo_thresh`.

### Window comparator

ALERT asserts when conversion is greater than `Hi_thresh` or less than `Lo_thresh`.

### Conversion-ready mode

Enable by:

* `Hi_thresh` MSB = `1`
* `Lo_thresh` MSB = `0`
* `COMP_QUE != 11`

Then ALERT/RDY provides conversion-ready signaling. In continuous mode, pulse width is approximately `8 µs` at conversion completion. 

## 12.3 VMeter Recommended Software Mode

For most VMeter firmware:

* use single-shot for low-power periodic measurements
* use continuous mode for streaming or high-rate logging
* disable comparator unless interrupt-driven acquisition is intentionally used

---

## 13. Reset Behavior

## 13.1 ADS1115 Power-On State

Documented register reset values:

* Conversion = `0x0000`
* Config = `0x8583`
* Lo_thresh = `0x8000`
* Hi_thresh = `0x7FFF`

After power-up, Conversion remains `0x0000` until first conversion completes.   

## 13.2 Software Reset

The ADS1115 has no dedicated reset register, but it does respond to the I2C general-call reset. When the device receives the general-call address `0x00` followed by the second byte `0x06` (`00000110b`), it performs an internal reset as if power-cycled: all Config register bits return to their default settings and the device enters the power-down state. Firmware may use this to restore the ADS1115 to a known reset state without toggling power.

Note: the general call resets every reset-capable device on the bus, not just the ADS1115; do not issue it if other shared peripherals must not be reset.

## 13.3 Module Reset

No dedicated board reset signal is documented. Reinitialization should be achieved by rewriting ADS1115 configuration.

---

## 14. Status and Diagnostics

## 14.1 ADS1115 Status Sources

* `OS` bit:

  * `0` conversion active
  * `1` idle / conversion complete
* Conversion register validity:

  * valid after first completed conversion
* ALERT/RDY status:

  * comparator or conversion-ready indication depending on configuration

## 14.2 Fault Conditions

Published constraints to enforce:

* ADS1115 analog inputs must not exceed `VDD + 0.3 V`
* ADS1115 digital inputs must not exceed `5.5 V`
* VMeter board measurement range is `±36 V`; driver must clamp or flag values outside calibrated range
* EEPROM must not be written
* Host I2C clock must not exceed `1 MHz` because of isolator path limitation

## 14.3 Suggested Driver Diagnostics

Software should detect and report:

* ADS1115 NACK / device missing at `0x49`
* EEPROM NACK / calibration storage missing at `0x53`
* timeout waiting for `OS = 1`
* suspicious all-zero conversions after initialization
* raw code saturation near `0x7FFF` or `0x8000`
* invalid calibration checksum, gain tag, or zero calibration value

---

## 15. Interrupts

ADS1115 supports ALERT/RDY on pin 2.

Interrupt-capable uses:

* comparator event output
* conversion-ready signal

Relevant configuration:

* `COMP_MODE`
* `COMP_POL`
* `COMP_LAT`
* `COMP_QUE`
* `Hi_thresh`
* `Lo_thresh`

Clearing behavior:

* nonlatching comparator clears automatically when within window
* latching comparator clears on conversion register read or SMBus alert response
* conversion-ready mode pulses at end of conversion in continuous mode; in single-shot mode pin asserts low at end of conversion when `COMP_POL = 0` 

The VMeter module documentation does not expose an interrupt pin on PORT.A, so normal use should assume polling rather than interrupts.

---

## 16. Operational Sequences

## 16.1 Minimal Raw ADC Initialization

1. Probe `0x49`.
2. Write Config with desired:

   * `MUX`
   * `PGA`
   * `MODE`
   * `DR`
   * comparator disabled: `COMP_QUE = 11`
3. For single-shot:

   * write `OS = 1`
   * wait conversion time
   * read Conversion register
4. Convert two's-complement code to raw ADC voltage using configured FSR.

## 16.2 Continuous Streaming

1. Configure `MODE = 0`
2. Select `DR`
3. Set pointer to Conversion register
4. Poll at or below configured output data rate
5. Read latest 16-bit sample

## 16.3 Board-Calibrated Voltage Read

1. Read raw ADC sample
2. Read and validate the active gain's block from EEPROM `0x53`
3. Apply the divider, absolute gain factor, and sign correction from section 10.3
4. Clamp/report range `±36 V`

---

## 17. Driver Initialization Sequence

Recommended startup sequence:

1. Configure host I2C bus to `≤ 1 MHz`; `400 kHz` is a conservative default.
2. Wait at least `50 µs` after VDD is stable (per ADS1115 power-up reset timing) before communicating; add more margin per system policy.
3. Probe ADC at `0x49`.
4. Optionally read current ADS1115 Config register.
5. Write known configuration:

   * pointer = Config register
   * comparator disabled (`COMP_QUE = 11`)
   * desired `MUX`, `PGA`, `MODE`, `DR`
6. Probe EEPROM at `0x53` and validate the active gain's calibration block.
7. Trigger one dummy conversion in single-shot mode.
8. Wait `1 / DR`.
9. Read conversion register and verify non-bus-fault behavior.
10. Mark driver initialized.

Recommended default raw configuration for conservative operation:

* `MUX = 000` if differential AIN0-AIN1 is intended
* `PGA = 010` (`±2.048 V`) unless calibration/front-end requirements dictate otherwise
* `MODE = 1` single-shot
* `DR = 100` (`128 SPS`)
* comparator disabled

The board uses differential AIN0-AIN1. M5Stack examples use `PGA = ±0.512 V` for measurements up to 16 V; choose gain from board-level range requirements and never exceed the module's ±36 V rating.

---

## 18. Runtime Operation Sequences

## 18.1 Read One Sample, Polling, Single-Shot

1. Build config with:

   * desired channel and gain
   * `MODE = 1`
   * `OS = 1`
2. Write Config register.
3. Wait `1 / DR` or poll `OS` until `1`.
4. Set pointer to Conversion register.
5. Read 2 bytes.
6. Sign-extend to `int16_t`.
7. Convert to volts at ADC input:
   `vin_adc = code * FSR / 32768`
8. Apply the active EEPROM calibration and board transfer formula.

## 18.2 Read Stream, Continuous

1. Write Config with `MODE = 0`.
2. Set pointer to Conversion register once.
3. Periodically read 2 bytes.
4. Convert using current FSR.
5. If changing FSR or MUX, rewrite Config and allow one full conversion period for fresh settled data.

## 18.3 Configure Conversion-Ready Mode

1. Write `Hi_thresh` with MSB `1`.
2. Write `Lo_thresh` with MSB `0`.
3. Set `COMP_QUE != 11`.
4. Optionally set `COMP_POL`.
5. Use ALERT/RDY if physically wired.

## 18.4 Comparator Threshold Update

Whenever `PGA` changes:

1. Recompute threshold register values in ADC codes for the new FSR.
2. Rewrite `Lo_thresh` and `Hi_thresh`.

---

## 19. Driver State Model

Driver should track at minimum:

* `i2c_addr_adc = 0x49`
* `i2c_addr_eeprom = 0x53`
* current ADS1115 pointer register target, if optimizing transfers
* cached config register image
* current:

  * mux
  * pga
  * mode
  * data rate
  * comparator settings
* calibration status:

  * unavailable
  * raw-only
  * calibration-loaded
* last raw code
* last converted ADC voltage
* last calibrated board voltage
* initialization state
* fault / timeout flags

---

## 20. Required Safety and Correctness Rules for Code Generation

1. Never write to EEPROM at `0x53`.
2. Never exceed host I2C bus speed of `1 MHz` on this board.
3. Always write Address Pointer before reading a different ADS1115 register.
4. Always write reserved pointer bits `7:2` as `0`.
5. Treat Conversion register as two's complement signed 16-bit.
6. Use the configured FSR when converting codes to voltage.
7. Wait at least one full conversion period after starting a single-shot conversion.
8. After changing `MUX`, `PGA`, or `MODE`, discard stale assumptions and wait for a fresh completed conversion.
9. Update threshold registers whenever `PGA` changes if comparator is enabled.
10. Validate the documented EEPROM block before using its gain factor.
11. Do not claim calibrated board voltage accuracy when calibration is unavailable or invalid.
12. Clamp or flag values outside published board range `±36 V`.
13. Handle NACK and conversion timeout paths explicitly.
14. Do not use ADS1115 high-speed I2C mode above the isolator limit.
15. Do not assume ALERT/RDY is externally accessible on this module.
16. Apply both the confirmed divider coefficient and the matching EEPROM calibration factor.

---

## 21. Canonical Constants

```c
#define VMETER_I2C_ADDR_ADC                 0x49u
#define VMETER_I2C_ADDR_EEPROM              0x53u

#define ADS1115_REG_CONVERSION              0x00u
#define ADS1115_REG_CONFIG                  0x01u
#define ADS1115_REG_LO_THRESH               0x02u
#define ADS1115_REG_HI_THRESH               0x03u

#define ADS1115_RESET_CONVERSION            0x0000u
#define ADS1115_RESET_CONFIG                0x8583u
#define ADS1115_RESET_LO_THRESH             0x8000u
#define ADS1115_RESET_HI_THRESH             0x7FFFu

#define VMETER_RANGE_VOLTS                  36.0f
#define VMETER_BOARD_ACCURACY_FS_PERCENT    1.0f
#define VMETER_BOARD_DIGITS_ERROR           1

#define VMETER_HOST_I2C_MAX_HZ              1000000u
#define VMETER_HOST_I2C_DEFAULT_HZ          400000u
```

---

## 22. Bitfield Constants

```c
#define ADS1115_CONFIG_OS_MASK              0x8000u
#define ADS1115_CONFIG_OS_SHIFT             15

#define ADS1115_CONFIG_MUX_MASK             0x7000u
#define ADS1115_CONFIG_MUX_SHIFT            12

#define ADS1115_CONFIG_PGA_MASK             0x0E00u
#define ADS1115_CONFIG_PGA_SHIFT            9

#define ADS1115_CONFIG_MODE_MASK            0x0100u
#define ADS1115_CONFIG_MODE_SHIFT           8

#define ADS1115_CONFIG_DR_MASK              0x00E0u
#define ADS1115_CONFIG_DR_SHIFT             5

#define ADS1115_CONFIG_COMP_MODE_MASK       0x0010u
#define ADS1115_CONFIG_COMP_MODE_SHIFT      4

#define ADS1115_CONFIG_COMP_POL_MASK        0x0008u
#define ADS1115_CONFIG_COMP_POL_SHIFT       3

#define ADS1115_CONFIG_COMP_LAT_MASK        0x0004u
#define ADS1115_CONFIG_COMP_LAT_SHIFT       2

#define ADS1115_CONFIG_COMP_QUE_MASK        0x0003u
#define ADS1115_CONFIG_COMP_QUE_SHIFT       0
```

Pointer register:

```c
#define ADS1115_PTR_RESERVED_MASK           0xFCu
#define ADS1115_PTR_P_MASK                  0x03u
#define ADS1115_PTR_P_SHIFT                 0
```

---

## 23. Enumerations

```c
typedef enum {
    ADS1115_MUX_DIFF_AIN0_AIN1 = 0,
    ADS1115_MUX_DIFF_AIN0_AIN3 = 1,
    ADS1115_MUX_DIFF_AIN1_AIN3 = 2,
    ADS1115_MUX_DIFF_AIN2_AIN3 = 3,
    ADS1115_MUX_SE_AIN0_GND   = 4,
    ADS1115_MUX_SE_AIN1_GND   = 5,
    ADS1115_MUX_SE_AIN2_GND   = 6,
    ADS1115_MUX_SE_AIN3_GND   = 7
} ads1115_mux_t;

typedef enum {
    ADS1115_PGA_6_144V = 0,
    ADS1115_PGA_4_096V = 1,
    ADS1115_PGA_2_048V = 2,
    ADS1115_PGA_1_024V = 3,
    ADS1115_PGA_0_512V = 4,
    ADS1115_PGA_0_256V_A = 5,
    ADS1115_PGA_0_256V_B = 6,
    ADS1115_PGA_0_256V_C = 7
} ads1115_pga_t;

typedef enum {
    ADS1115_MODE_CONTINUOUS = 0,
    ADS1115_MODE_SINGLESHOT = 1
} ads1115_mode_t;

typedef enum {
    ADS1115_DR_8   = 0,
    ADS1115_DR_16  = 1,
    ADS1115_DR_32  = 2,
    ADS1115_DR_64  = 3,
    ADS1115_DR_128 = 4,
    ADS1115_DR_250 = 5,
    ADS1115_DR_475 = 6,
    ADS1115_DR_860 = 7
} ads1115_dr_t;

typedef enum {
    ADS1115_COMP_TRADITIONAL = 0,
    ADS1115_COMP_WINDOW      = 1
} ads1115_comp_mode_t;

typedef enum {
    ADS1115_COMP_ACTIVE_LOW  = 0,
    ADS1115_COMP_ACTIVE_HIGH = 1
} ads1115_comp_pol_t;

typedef enum {
    ADS1115_COMP_NONLATCHING = 0,
    ADS1115_COMP_LATCHING    = 1
} ads1115_comp_lat_t;

typedef enum {
    ADS1115_COMP_ASSERT_1    = 0,
    ADS1115_COMP_ASSERT_2    = 1,
    ADS1115_COMP_ASSERT_4    = 2,
    ADS1115_COMP_DISABLE     = 3
} ads1115_comp_que_t;

typedef enum {
    VMETER_CAL_UNAVAILABLE = 0,
    VMETER_CAL_RAW_ONLY    = 1,
    VMETER_CAL_LOADED      = 2
} vmeter_cal_state_t;
```

---

## 24. Minimal Functional Feature Set

A minimally correct VMeter driver must provide:

1. I2C communication with ADS1115 at `0x49`
2. ADS1115 register read/write support
3. Single-shot conversion support
4. Continuous conversion support
5. Raw signed 16-bit sample readout
6. ADC-input-voltage conversion using FSR
7. Optional EEPROM presence detection at `0x53`
8. Explicit write protection policy for EEPROM
9. Error handling for NACK and timeout
10. Configuration of `MUX`, `PGA`, `MODE`, and `DR`

A fully correct board-level voltage driver additionally requires:
11. EEPROM block validation and per-gain calibration
12. Confirmed divider and sign correction
13. Enforcement of board range `±36 V`

---

## 25. Final Implementation Intent

A generated driver should behave as a safe, deterministic ADS1115-based VMeter board driver with correct I2C transactions, correct two's-complement handling, correct timing, and a hard prohibition against writing the factory calibration EEPROM.

Common mistakes to avoid:

* using the wrong device address (`0x48` instead of board address `0x49`)
* exceeding `1 MHz` bus speed on the isolated board
* treating ADS1115 output as unsigned
* not waiting for conversion completion
* forgetting to set the pointer register before reads
* changing PGA without updating threshold interpretation
* assuming comparator or ALERT/RDY are always physically usable
* accepting calibration with a bad checksum, wrong gain tag, or zero divisor
* deriving final board voltage from the divider while ignoring calibration storage
