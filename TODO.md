# TODO

Possible future integrations and improvements.

## Integrations

- MQTT/WiFi publishing: optional network mode for sending battery data to an
  MQTT broker. This is intentionally not active in the current firmware.
- LoRa telemetry: optional long-range transport for the same main battery data
  currently sent over ESP-NOW.

## Firmware

- Add a receiver-side timeout indicator for stale ESP-NOW data.
- Add a simple I2C scanner helper for LCD address discovery.
- Add optional pairing/filtering by sender MAC address if broadcast reception
  becomes too noisy.
