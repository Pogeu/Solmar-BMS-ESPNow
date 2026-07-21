# Solmar BMS Gateway

Repositório dos firmwares e da página de telemetria da bateria do projeto
Solmar. O objetivo do sistema é ser a interface entre a bateria do barco e as
pessoas que precisam acompanhar seu estado: o usuário perto do barco pelo LCD e
a equipe remota pelo dashboard MQTT.

> [!NOTE]  
> O modelo da bateria que esta sendo utilizada é **Felicity FLA12171-EU**.

ESP-NOW, MQTT/WiFi, microSD e futuras conexões GSM são meios de transportar ou
registrar os dados. O foco do projeto é entregar leitura clara, local e remota,
dos dados do BMS.

O repositório contém:

- `firmware/gateway`: lê os dados do BMS Felicity/Felicity ESS via RS485 Modbus
  e atua como origem da telemetria.
- `firmware/receiver-lcd`: mostra os principais valores em um display grafico
  128x64 SPI para quem esta perto do barco. Nesta topologia ele recebe os dados
  por ESP-NOW.
- `dashboard`: página web para a equipe acompanhar a bateria a distancia por
  MQTT.
- `shared`: formato comum do pacote ESP-NOW usado quando o display local fica em um
  segundo ESP32-S3.

## Visão geral

O gateway fica conectado ao barramento RS485 da bateria e transforma as leituras
do BMS em informação de uso:

- Display local: leitura rápida para operação e diagnóstico perto do barco.
- Dashboard remoto: visão para a equipe quando ela não esta no barco.
- Log microSD: histórico simples em JSON Lines para análise posterior.
- ESP-NOW: transporte local sem roteador quando o LCD esta em outra placa.
- MQTT/WiFi: transporte atual para o dashboard remoto.

O ambiente principal hoje é `esp32-s3-gateway-lcd-direct`: uma placa lê a BMS
via RS485, atualiza o display local e grava no microSD. As variantes
`esp32-s3-gateway-lcd-direct-mqtt` e `esp32-s3-gateway-lcd-direct-lora-*`
acrescentam telemetria remota sem mudar a leitura da bateria.

Possíveis integrações futuras ficam listadas em [TODO.md](TODO.md), incluindo
LoRa e troca do backend WiFi por GSM.

## Gateway RS485

O gateway é a placa conectada ao barramento RS485 da bateria. Ele pode operar em
duas topologias principais:

- `esp32-s3-gateway`: lê a BMS e envia um pacote ESP-NOW para outro ESP32-S3.
- `esp32-s3-gateway-lcd-direct`: lê a BMS, atualiza o display local e grava
  microSD. Variantes do mesmo alvo adicionam MQTT ou LoRa.

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

Compilar o gateway ESP32-S3 com saída ESP-NOW sem fazer upload:

```sh
cd firmware/gateway
pio run -e esp32-s3-gateway
```

Fazer upload do gateway ESP32-S3:

```sh
cd firmware/gateway
pio run -e esp32-s3-gateway -t upload
```

Fazer upload em uma porta específica:

```sh
pio run -e esp32-s3-gateway -t upload --upload-port COM5
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

## Display local

O display local é a interface para quem esta perto do barco. Ele mostra SOC,
potência, tensão, corrente, temperatura, status de carga/descarga, falhas e
idade da última leitura.

Existem duas formas de usar o display:

- Display direto no gateway: usado pelo ambiente `esp32-s3-gateway-lcd-direct`.
- Display em uma segunda placa: usado pelo `firmware/receiver-lcd`, recebendo dados
  por ESP-NOW.

Desconecte a placa do gateway ou use `--upload-port COMx` para evitar gravar o
firmware do receptor na placa errada.

Compilar o receptor LCD ESP32-S3 sem fazer upload:

```sh
cd firmware/receiver-lcd
pio run -e esp32-s3-lcd-receiver
```

Fazer upload do receptor LCD ESP32-S3:

```sh
cd firmware/receiver-lcd
pio run -e esp32-s3-lcd-receiver -t upload
```

Fazer upload em uma porta específica:

```sh
pio run -e esp32-s3-lcd-receiver -t upload --upload-port COM6
```

Abrir o monitor serial do receptor LCD:

```sh
pio device monitor -b 115200
```

## ESP-NOW para display separado

ESP-NOW é usado quando o display local fica em uma segunda placa ESP32-S3. Ele não
é a finalidade do projeto, mas uma forma prática de levar os dados do gateway
até o display sem roteador, SSID, senha ou broker.

Nesse caso, receptor e transmissor precisam usar o mesmo canal ESP-NOW.

A configuração do gateway é `ESP_NOW_WIFI_CHANNEL` em
`firmware/gateway/platformio.ini`. O receptor tem a mesma configuração em
`firmware/receiver-lcd/platformio.ini`. O valor padrão é o canal `1` nos dois
firmwares.

Se mudar o canal, altere nos dois firmwares:

```ini
build_flags =
	-D ESP_NOW_WIFI_CHANNEL=6
```

Se o receptor continuar mostrando `Sem dados`, confira primeiro:

- o gateway está ligado e lendo a bateria
- as duas placas usam o mesmo canal ESP-NOW
- o receptor recebeu o firmware do receptor, não o firmware do gateway
- a pinagem SPI do display corresponde ao `platformio.ini`

## Gateway com display direto, microSD e telemetria opcional

O ambiente `esp32-s3-gateway-lcd-direct` usa uma única placa ESP32-S3 conectada
ao RS485 da bateria e ao display grafico SPI. Esse é o caminho principal para
transformar a leitura da bateria em informação local. Nesse modo o ESP-NOW fica
desativado, e o firmware também grava cada leitura do BMS no microSD em JSON
Lines. As variantes `-mqtt` e `-lora-*` adicionam telemetria remota:

```text
/bms_log.jsonl
```

Cada linha é um objeto JSON independente com o schema
`solmar.bms.reading.v1`. Esse formato foi escolhido em vez de CSV porque as
leituras têm tipos diferentes e arrays de células/temperaturas. O mesmo objeto
JSON é usado como payload MQTT para o dashboard.

Pinagem configurada para o display ST7565 / GMG12864-06D:

| Pin # | Symbol | ESP32-S3 |
|---|---|---|
| `1` | `CS` | `GPIO15` |
| `2` | `RST` | `GPIO17` |
| `3` | `RS (A0)` | `GPIO16` |
| `4` | `SCL` | `GPIO4` |
| `5` | `SI` | `GPIO6` |
| `6` | `VDD` | `3V3` |
| `7` | `GND` | `GND` |
| `8` | `LEDA` | `3V3` ou `VCC` do backlight |
| `9` | `LEDK` | `GND` |
| `10` | `IC_SCK` | `nao usar` |
| `11` | `IC_CS` | `nao usar` |
| `12` | `IC_SDO` | `nao usar` |
| `13` | `IC_SDI` | `nao usar` |

No firmware, a comunicacao principal do display fica somente em `CS`, `RST`,
`RS (A0)`, `SCL` e `SI`.

Pinagem configurada para o módulo microSD SPI:

| microSD | ESP32-S3 |
|---|---|
| `3v3` | `3V3` |
| `GND` | `GND` |
| `CS` | `GPIO7` |
| `MOSI` | `GPIO6` |
| `CLK` | `GPIO4` |
| `MISO` | `GPIO5` |

No modo direto, display e microSD compartilham o mesmo barramento SPI fisico:
`CLK/SCL` em `GPIO4` e `MOSI/SI` em `GPIO6`. Cada um fica com seu `CS`
separado, e só o microSD usa `MISO` em `GPIO5`.

O botão de páginas do display foi movido para `GPIO10` para deixar livre o
barramento SPI compartilhado.

Os pinos ficam em `firmware/gateway/platformio.ini`:

```ini
-D DISPLAY_SPI_SCK_PIN=4
-D DISPLAY_SPI_MOSI_PIN=6
-D DISPLAY_CS_PIN=15
-D DISPLAY_DC_PIN=16
-D DISPLAY_RESET_PIN=17
-D DISPLAY_PAGE_BUTTON_PIN=10
-D SD_LOG_USE_DEFAULT_SPI_PINS=0
-D SD_LOG_CS_PIN=7
-D SD_LOG_SCK_PIN=4
-D SD_LOG_MISO_PIN=5
-D SD_LOG_MOSI_PIN=6
```

Compilar o gateway com LCD direto e microSD:

```sh
cd firmware/gateway
pio run -e esp32-s3-gateway-lcd-direct
```

Fazer upload:

```sh
pio run -e esp32-s3-gateway-lcd-direct -t upload
```

Se o cartão não inicializar, o firmware continua lendo o BMS e atualizando o
display; o erro aparece no monitor serial com prefixo `[SD]`.

### Teste local das telas do display

Sem ligar a bateria, voce pode subir um firmware de teste que injeta leituras
falsas nas mesmas telas do modo direto. Esse ambiente nao usa RS485, microSD ou
MQTT e serve para validar layout, troca de paginas e leitura visual do display:

```sh
cd firmware/gateway
pio run -e esp32-s3-gateway-display-test
pio run -e esp32-s3-gateway-display-test -t upload
pio device monitor -b 9600
```

O botao em `GPIO10` continua trocando as paginas, e o monitor serial mostra os
valores sinteticos enviados para o display.

### Dashboard remoto por MQTT

O ambiente `esp32-s3-gateway-lcd-direct-mqtt` publica cada leitura em MQTT. A
conexao WiFi usa WiFiManager: na primeira configuracao, ou se nao
houver credenciais salvas, o ESP abre o portal `Solmar-BMS-Setup` por ate 120
segundos. Se o WiFi nao for configurado, a leitura RS485, o LCD e o microSD
continuam funcionando.

Configuracao padrao em `firmware/gateway/platformio.ini`:

```ini
-D BMS_MQTT_ENABLE=1
-D BMS_MQTT_HOST=\"broker.hivemq.com\"
-D BMS_MQTT_PORT=1883
-D BMS_MQTT_TOPIC_BASE=\"solmar/bms/felicity-fla12171\"
```

Os payloads usam o mesmo schema JSON `solmar.bms.reading.v1` do log em microSD
e sao publicados como mensagens retidas nos topicos:

```text
solmar/bms/felicity-fla12171/readings/v1/<device_id>/<tipo>
```

A pagina em `dashboard/index.html` assina por WebSocket o filtro:

```text
solmar/bms/felicity-fla12171/readings/v1/+/+
```

O codigo MQTT foi separado em `firmware/gateway/src/bms_mqtt_publisher.cpp`.
Para implementar GSM no futuro, troque o backend de rede desse modulo por um
cliente compativel com `Client`, como TinyGSM, sem alterar o parser do BMS nem a
pagina.

### Envio por LoRa E90-DTU ou E220

O modo direto tambem tem duas variantes com envio LoRa por UART transparente.
Elas reutilizam o payload JSON `solmar.bms.reading.v1`, mas por padrao enviam
somente mensagens `battery_info` a cada 5 segundos para evitar ocupar o enlace
LoRa com dados grandes de celulas e limites.

Ambientes:

```sh
cd firmware/gateway
pio run -e esp32-s3-gateway-lcd-direct-lora-e90-dtu
pio run -e esp32-s3-gateway-lcd-direct-lora-e220
```

Upload:

```sh
pio run -e esp32-s3-gateway-lcd-direct-lora-e90-dtu -t upload
pio run -e esp32-s3-gateway-lcd-direct-lora-e220 -t upload
```

Pinagem do E90-DTU usando a porta RS485 do DTU com um transceiver RS485 no
ESP32-S3:

| Sinal local | ESP32-S3 |
|---|---|
| RX do ESP32-S3, vindo do RO do transceiver | GPIO14 |
| TX do ESP32-S3, indo para DI do transceiver | GPIO13 |
| DE + RE do transceiver | GPIO12 |

Se usar a porta RS232 do E90-DTU com MAX3232, remova o controle local DE/RE no
ambiente ajustando `BMS_LORA_E90_DE_RE_PIN=-1`.

Pinagem do E220-900T22D:

| E220 | ESP32-S3 |
|---|---|
| TXD -> RX do ESP32-S3 | GPIO14 |
| RXD <- TX do ESP32-S3 | GPIO13 |
| M0 | GPIO11 |
| M1 | GPIO12 |
| AUX | GPIO18 |

Os dois modulos do par precisam estar configurados com os mesmos parametros de
radio e baud serial. Esta implementacao nao tenta fazer E90-DTU conversar com
E220; use par igual com par igual.

Codigos simples de bancada para testar envio e recebimento ficam em
`firmware/lora-tests`. Eles funcionam em ESP32 e Arduino UNO/Nano e imprimem no
monitor serial tudo que chega pelo radio.


## Tela touch CYD ESP32-2432S028R

O firmware `firmware/cyd-display` e a interface local nova para a placa
ESP32-2432S028R / Cheap Yellow Display. Ele substitui o LCD 128x64 quando a
tela fica em uma segunda placa, recebe os pacotes `EspNowBatteryPacket` por
ESP-NOW e atualiza somente as areas dinamicas para reduzir flicker.

A parte inferior da tela troca entre quatro paineis: resumo, comunicacao,
celulas e GPS/enlace. A troca funciona pelo toque na tela ou pelo botao BOOT.
O SD onboard da placa CYD fica reservado para uma etapa futura de log local.

Pinagem configurada:

| Funcao | ESP32 |
|---|---|
| TFT MISO | GPIO12 |
| TFT MOSI | GPIO13 |
| TFT SCLK | GPIO14 |
| TFT CS | GPIO15 |
| TFT DC | GPIO2 |
| TFT BL | GPIO21 |
| Touch CS | GPIO33 |
| Touch CLK | GPIO25 |
| Touch MISO / T_DO | GPIO39 |
| Touch MOSI / T_DIN | GPIO32 |
| Touch IRQ | GPIO36 |
| Botao BOOT / pagina | GPIO0 |

Build e upload:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\cyd-display -e esp32-cyd-battery-screen
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\cyd-display -e esp32-cyd-battery-screen -t upload --upload-port COM8
```

Modo demo sem central:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\cyd-display -e esp32-cyd-battery-screen-demo -t upload --upload-port COM8
```

## Fontes usadas

| Fonte | O que foi usado no projeto |
|---|---|
| [Smartsmurf/FelicityBMS2MQTT](https://github.com/Smartsmurf/FelicityBMS2MQTT) | Base principal do RS485/Modbus da BMS. Usado para os registradores `0xF80B`, `0x1302`, `0x131C` e `0x132A`, além das escalas de tensão, corrente, limites, células, temperaturas e flags. |
| [alexbenisch/felicity-bms](https://github.com/alexbenisch/felicity-bms) | Confirmou os comandos RS485 `0xF80B`, `0x1302` e `0x132A`, os offsets internos de tensão/corrente/SOC e o fallback `0x132A len 0x14` para ler 16 slots de célula + 4 temperaturas. |
| [mr-manuel/venus-os_dbus-serialbattery](https://github.com/mr-manuel/venus-os_dbus-serialbattery) | Não foi usado como mapa Modbus, mas ajudou a confirmar o comportamento da BMS Felicity: células em mV, temperaturas filtrando `0x7FFF` e existência de campos internos como versão, modelo, serial, warnings e faults via BLE. |
| [Manual Felicity FLA12171-EU](./FLA12171-EU%20User%20Guide%20-%20English.pdf) | Usado para validar o modelo da bateria, tensão nominal, faixa de operação, limites elétricos, comunicação RS485/CAN e pinagem do conector. |
