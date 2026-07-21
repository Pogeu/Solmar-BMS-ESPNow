# TODO

Project task list for the Solmar BMS Gateway firmwares and dashboard.

### Todo

- [ ] Validate live LoRa telemetry with the physical E90-DTU and E220 pairs [#4](https://github.com/Pogeu/Solmar-BMS-Gateway/issues/4) ~4h #test #lora
- [ ] Validate A7670SA MQTT, GNSS and NEO-6M fallback in the remote field setup ~4h #test #gps #mqtt
- [ ] Decide and implement local SD logging on the CYD display board ~4h #storage #cyd

- [ ] Add stale-data indicator on the LCD receiver [#5](https://github.com/Pogeu/Solmar-BMS-Gateway/issues/5) ~4h #ux
  - [ ] Show when the last ESP-NOW packet is older than the timeout

- [ ] Add SPI display diagnostic page for ST7565 setup [#6](https://github.com/Pogeu/Solmar-BMS-Gateway/issues/6) ~2h #tooling
  - [ ] Print detected LCD backpack addresses on serial monitor

- [ ] Add optional sender MAC filtering [#7](https://github.com/Pogeu/Solmar-BMS-Gateway/issues/7) ~4h #feat #espnow
  - [ ] Accept packets only from a configured gateway MAC address

### In Progress

- [ ] Validate upload and live ESP-NOW reception on the physical ESP32-S3 boards [#8](https://github.com/Pogeu/Solmar-BMS-Gateway/issues/8) ~2h #test

### Done ✓

- [x] Create organized firmware repository ~1d #refactor
- [x] Split gateway and LCD receiver into separate PlatformIO projects ~4h #refactor
- [x] Share the ESP-NOW battery packet format between sender and receiver ~3h #protocol
- [x] Implement ESP32-S3 gateway with RS485 input and ESP-NOW output ~1d #feat
- [x] Remove active MQTT/WiFi publishing from the gateway firmware ~2h #cleanup
- [x] Implement ESP32-S3 local display receiver ~1d #feat
- [x] Add ESP-NOW packet unit test build ~3h #test
- [x] Add WiFiManager/MQTT publishing for the LCD-direct gateway ~2d #feat #mqtt
- [x] Add web dashboard for remote battery monitoring ~1d #feat #dashboard
- [x] Add optional LoRa telemetry for E90-DTU and E220-900T22D ~3d #feat #lora
- [x] Add ESP32-S3 central with A7670SA, NEO-6M fallback GPS, LoRa, MQTT and OTA ~3d #feat #gateway
- [x] Add dashboard map, dark mode, cell radar chart and user-to-boat distance ~1d #feat #dashboard
- [x] Add CYD touch display receiver with partial redraw and page switching ~1d #feat #cyd
