# Solmar BMS ESP-NOW

Monorepo for the Solmar battery telemetry firmware.

It contains two independent PlatformIO firmwares plus one shared protocol
header:

- `firmware/gateway`: reads Felicity/Felicity ESS BMS data over RS485 Modbus
  and broadcasts the main battery data over ESP-NOW.
- `firmware/receiver-lcd`: receives the ESP-NOW battery packet on a second
  ESP32-C3 and shows the main values on a 16x2 I2C LCD.
- `shared`: common ESP-NOW packet format used by both firmwares.

The active firmware does not publish data through MQTT and does not connect to
a WiFi network. ESP-NOW still uses the ESP32 radio internally, but it does not
need a router, SSID, password or MQTT broker.

Keeping the ESP-NOW packet in `shared/espnow_battery_packet.h` avoids a common
embedded bug: the sender and receiver silently drifting to different binary
layouts.

Possible future integrations are tracked in [TODO.md](TODO.md), including
MQTT/WiFi publishing and LoRa.

## Gateway

The gateway is the board connected to the battery RS485 bus.

If the `pio` command is not available in your terminal, replace `pio` with the
full PlatformIO path:

```sh
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe
```

Before uploading, list the connected serial ports:

```sh
pio device list
```

If only one ESP32 is connected, PlatformIO usually detects the port
automatically. If more than one board is connected, pass the upload port
explicitly with `--upload-port COMx`.

Build the ESP32-C3 gateway without uploading:

```sh
cd firmware/gateway
pio run -e esp32-c3-gateway
```

Upload the ESP32-C3 gateway:

```sh
cd firmware/gateway
pio run -e esp32-c3-gateway -t upload
```

Upload it to a specific port:

```sh
pio run -e esp32-c3-gateway -t upload --upload-port COM5
```

Open the serial monitor for this gateway:

```sh
pio device monitor -b 9600
```

Compile the ESP-NOW packet unit test:

```sh
cd firmware/gateway
pio test -e espnow-packet-test --without-uploading --without-testing
```

## LCD Receiver

The LCD receiver is the second ESP32-C3. It is not connected to RS485. It only
receives ESP-NOW packets and updates the 16x2 I2C display.

Disconnect the gateway board or use `--upload-port COMx` so you do not upload
the receiver firmware to the wrong board.

Build the ESP32-C3 LCD receiver without uploading:

```sh
cd firmware/receiver-lcd
pio run -e esp32-c3-lcd-receiver
```

Upload the ESP32-C3 LCD receiver:

```sh
cd firmware/receiver-lcd
pio run -e esp32-c3-lcd-receiver -t upload
```

Upload it to a specific port:

```sh
pio run -e esp32-c3-lcd-receiver -t upload --upload-port COM6
```

Open the serial monitor for the LCD receiver:

```sh
pio device monitor -b 115200
```

## ESP-NOW Channel

The receiver and sender must use the same ESP-NOW channel.

The gateway setting is `ESP_NOW_WIFI_CHANNEL` in
`firmware/gateway/platformio.ini`. The receiver has the same setting in
`firmware/receiver-lcd/platformio.ini`. The default value is channel `1` in
both firmwares.

If you change the channel, change it in both firmwares:

```ini
build_flags =
	-D ESP_NOW_WIFI_CHANNEL=6
```

If the LCD receiver keeps showing `Sem dados`, check these items first:

- the gateway is powered and reading the battery
- both boards use the same ESP-NOW channel
- the receiver was uploaded with the receiver firmware, not the gateway firmware
- the LCD I2C address is correct (`0x27` or `0x3F` are common)
