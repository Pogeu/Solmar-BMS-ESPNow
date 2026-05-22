# Solmar BMS ESP-NOW

Repositório único dos firmwares de telemetria da bateria do projeto Solmar.

> [!NOTE]  
> O modelo da bateria que esta sendo utilizada é Felicity FLA12171-EU.

O repositório contém dois firmwares PlatformIO independentes e um cabeçalho de
protocolo compartilhado:

- `firmware/gateway`: lê os dados do BMS Felicity/Felicity ESS via RS485 Modbus
  e transmite os dados principais da bateria por ESP-NOW.
- `firmware/receiver-lcd`: recebe o pacote ESP-NOW em um segundo ESP32-C3 e
  mostra os principais valores em um LCD 16x2 I2C.
- `shared`: formato comum do pacote ESP-NOW usado pelos dois firmwares.

O firmware ativo não publica dados por MQTT e não conecta em uma rede WiFi.
O ESP-NOW usa internamente o rádio do ESP32, mas não precisa de roteador, SSID,
senha ou broker MQTT.

Manter o pacote ESP-NOW em `shared/espnow_battery_packet.h` evita um erro comum
em projetos embarcados: transmissor e receptor evoluírem para formatos binários
diferentes sem perceber.

Possíveis integrações futuras ficam listadas em [TODO.md](TODO.md), incluindo
publicação MQTT/WiFi e LoRa.

## Gateway

O gateway é a placa conectada ao barramento RS485 da bateria.

Se o comando `pio` não estiver disponível no terminal, substitua `pio` pelo
caminho completo do PlatformIO:

```sh
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe
```

Antes de fazer upload, liste as portas seriais conectadas:

```sh
pio device list
```

Se apenas um ESP32 estiver conectado, o PlatformIO geralmente detecta a porta
automaticamente. Se houver mais de uma placa conectada, informe a porta
explicitamente com `--upload-port COMx`.

Compilar o gateway ESP32-C3 sem fazer upload:

```sh
cd firmware/gateway
pio run -e esp32-c3-gateway
```

Fazer upload do gateway ESP32-C3:

```sh
cd firmware/gateway
pio run -e esp32-c3-gateway -t upload
```

Fazer upload em uma porta específica:

```sh
pio run -e esp32-c3-gateway -t upload --upload-port COM5
```

Abrir o monitor serial do gateway:

```sh
pio device monitor -b 9600
```

Compilar o teste unitário do pacote ESP-NOW:

```sh
cd firmware/gateway
pio test -e espnow-packet-test --without-uploading --without-testing
```

## Verificações no GitHub

O repositório tem um workflow para pull requests:

- `.github/workflows/ci.yml`: compila o firmware do gateway, compila os testes
  PlatformIO e compila o receptor LCD.

## Receptor LCD

O receptor LCD é o segundo ESP32-C3. Ele não é conectado ao RS485. Ele apenas
recebe pacotes ESP-NOW e atualiza o display LCD 16x2 I2C.

Desconecte a placa do gateway ou use `--upload-port COMx` para evitar gravar o
firmware do receptor na placa errada.

Compilar o receptor LCD ESP32-C3 sem fazer upload:

```sh
cd firmware/receiver-lcd
pio run -e esp32-c3-lcd-receiver
```

Fazer upload do receptor LCD ESP32-C3:

```sh
cd firmware/receiver-lcd
pio run -e esp32-c3-lcd-receiver -t upload
```

Fazer upload em uma porta específica:

```sh
pio run -e esp32-c3-lcd-receiver -t upload --upload-port COM6
```

Abrir o monitor serial do receptor LCD:

```sh
pio device monitor -b 115200
```

## Canal ESP-NOW

O receptor e o transmissor precisam usar o mesmo canal ESP-NOW.

A configuração do gateway é `ESP_NOW_WIFI_CHANNEL` em
`firmware/gateway/platformio.ini`. O receptor tem a mesma configuração em
`firmware/receiver-lcd/platformio.ini`. O valor padrão é o canal `1` nos dois
firmwares.

Se mudar o canal, altere nos dois firmwares:

```ini
build_flags =
	-D ESP_NOW_WIFI_CHANNEL=6
```

Se o receptor LCD continuar mostrando `Sem dados`, confira primeiro:

- o gateway está ligado e lendo a bateria
- as duas placas usam o mesmo canal ESP-NOW
- o receptor recebeu o firmware do receptor, não o firmware do gateway
- o endereço I2C do LCD está correto (`0x27` e `0x3F` são comuns)


## Fontes usadas

| Fonte | O que foi usado no projeto |
|---|---|
| [Smartsmurf/FelicityBMS2MQTT](https://github.com/Smartsmurf/FelicityBMS2MQTT) | Base principal do RS485/Modbus da BMS. Usado para os registradores `0xF80B`, `0x1302`, `0x131C` e `0x132A`, além das escalas de tensão, corrente, limites, células, temperaturas e flags. |
| [alexbenisch/felicity-bms](https://github.com/alexbenisch/felicity-bms) | Confirmou os comandos RS485 `0xF80B`, `0x1302` e `0x132A`, os offsets internos de tensão/corrente/SOC e o fallback `0x132A len 0x14` para ler 16 slots de célula + 4 temperaturas. |
| [mr-manuel/venus-os_dbus-serialbattery](https://github.com/mr-manuel/venus-os_dbus-serialbattery) | Não foi usado como mapa Modbus, mas ajudou a confirmar o comportamento da BMS Felicity: células em mV, temperaturas filtrando `0x7FFF` e existência de campos internos como versão, modelo, serial, warnings e faults via BLE. |
| [Manual Felicity FLA12171-EU](./FLA12171-EU%20User%20Guide%20-%20English.pdf) | Usado para validar o modelo da bateria, tensão nominal, faixa de operação, limites elétricos, comunicação RS485/CAN e pinagem do conector. |
