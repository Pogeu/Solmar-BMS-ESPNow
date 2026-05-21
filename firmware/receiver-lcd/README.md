# ESP-NOW LCD Receiver

Firmware for a second ESP32-C3 that receives the battery broadcast sent by the
gateway over ESP-NOW and shows the main values on a 16x2 I2C LCD.

Default wiring:

- LCD VCC -> 5V or 3V3, according to the LCD backpack
- LCD GND -> GND
- LCD SDA -> GPIO8
- LCD SCL -> GPIO9

Default settings in `platformio.ini`:

- ESP-NOW channel: `1`
- LCD I2C address: `0x27`
- LCD size: `16x2`

If the LCD does not show text, try changing `LCD_I2C_ADDR` to `0x3F`.
The ESP-NOW channel must match `ESP_NOW_WIFI_CHANNEL` in the sender firmware.
The binary packet format is shared through `../../shared/espnow_battery_packet.h`.

Build and upload:

```sh
pio run -e esp32-c3-lcd-receiver
pio run -e esp32-c3-lcd-receiver -t upload
pio device monitor -b 115200
```

If more than one ESP32 is connected, list the ports first:

```sh
pio device list
```

Then upload to the receiver port explicitly:

```sh
pio run -e esp32-c3-lcd-receiver -t upload --upload-port COM6
```

If the display keeps showing `Sem dados`, confirm that the sender and receiver
use the same `ESP_NOW_WIFI_CHANNEL`.
