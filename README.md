# Smart Meter BLE Sensor using Cypress PSoC 4200 BLE

Creates a Bluetooth Low Energy (BLE) device connecting to the Dutch DSMR port P1 on new smart power meters. This project is build around the Cypress PSoC 42000 BLE (CY8C4248LQI-BL583) BLE device, providing BLE 4.2.
The device reads the DSMR data every 30 seconds, allowing external device to retrieve this or wait for notifications.

## Functions

* Support DSMR v4/v5 signaling (8N1, 115200 baud)
  * Power consumption and delivery meters
  * Current tariff
  * Instantaneous power
  * Instantaneous phase info (current, voltage, power consumption, power delivery)
  * Gas consumption
  * Time when data was retrieved
* Per-device BLE numeric code needed for pairing
* Very low power, as device is mostly in deep sleep mode

## Future function

* Automatically detect DSMR version (baud rate) and inverted/non-inverted input.
* Support more fields support by DSMR
* Programable DSMR update interval
* UTC / Local time handling
* Use button to enable 2 minute pairing period
* OTA function

## Connections

Design goals are to use minimal power and minimal (external) components.
LEDS should be connected to Vdd with appropriate resistor.
Button should pull down to ground when pressed.
No other components are needed, but resistors can be used to protect circuity for development.

For development kit, use [CY8CKIT-042-BLE-A|https://www.cypress.com/documentation/development-kitsboards/cy8ckit-143a-psoc-4-ble-256kb-module-bluetooth-42-radio].
Uses no additional passive component to connect to DSMR meter, as it support 5 volt operation and integrates inverter for serial data.

| Port  | BL583 pin | Function                |
|-------|-----------|------------------------ |
| P3[7] | 54        | Meter LED: indicates operation with meter is active. Should flash every 30 seconds. |
| P3[6] | 53        | Advertising LED. |
| P2[6] | 43        | Disconnect LED: No BLE peer connected. |
| P2[7] | 44        | Button: When pressed on poweron, factory reset. |
| P1[5] | 33        | uart_tx: debug output. |
| P1[1] | 29        | Meter Request: Connect to DSMR RTS line (pin 2). |
| P1[0] | 28        | Meter Request: Connect to DSMR RTS line (pin 2). |
| P0[1] | 20        | Meter Serial In: Connect to DSMR TXD line (pin 5). |
| P0[0] | 19        | Internal: Serial invert, connect to P0[2]. |
| P0[2] | 21        | Internal: Serial invert, connect to P0[0]. |

On the DSMR P1 port, use a RJ12 (6P6C) connector, providing:

| pin | Function                                          |
|-----|-------------------------------------------------- |
| 1   | Vcc (+5 volt)                                     |
| 2   | RTS: Connect to P1[0] and P1[1]                   |
| 3   | GND(data): Data ground                            |
| 4   | not connected                                     |
| 5   | TXD: Connect to P0[1]                             |
| 6   | GND(Vcc): Power ground                            |

## Build With

* Cypress PSoC Creator 4.2 or later
* Cypress Peripheral Driver Library 2.1.0
