# Gateway Firmware

Reads Felicity/Felicity ESS BMS values over RS485 Modbus and broadcasts the
main battery data over ESP-NOW.

This firmware intentionally does not publish through MQTT and does not connect
to a WiFi network. ESP-NOW uses the ESP32 radio directly, without SSID,
password, router or MQTT broker.

Build the ESP32-C3 gateway:

```sh
pio run -e esp32-c3-gateway
```

Upload the ESP32-C3 gateway:

```sh
pio run -e esp32-c3-gateway -t upload
```

If more than one board is connected, list ports and choose one explicitly:

```sh
pio device list
pio run -e esp32-c3-gateway -t upload --upload-port COM5
```

Open the monitor:

```sh
pio device monitor -b 9600
```

Compile the packet unit test:

```sh
pio test -e espnow-packet-test --without-uploading --without-testing
```

Run the packet unit test on a connected ESP32-C3:

```sh
pio test -e espnow-packet-test
```

The binary ESP-NOW packet shared with the receiver lives in
`../../shared/espnow_battery_packet.h`.
