# Firmware Gateway

Lê os valores do BMS Felicity/Felicity ESS via RS485 Modbus e transforma esses
dados em informação para o usuário local e para a equipe remota.

Este projeto tem dois ambientes principais neste diretório:

- `esp32-s3-gateway`: lê a BMS e transmite um resumo por ESP-NOW para um display em
  outra placa ESP32-S3.
- `esp32-s3-gateway-lcd-direct`: lê a BMS, atualiza um display conectado na mesma
  placa e grava JSON Lines no microSD. Variantes desse alvo adicionam MQTT ou LoRa.

ESP-NOW é uma opção de transporte local para separar a placa do gateway da
placa do LCD. O objetivo maior do firmware é servir como gateway entre a BMS e
as interfaces de uso do projeto.

Compilar o gateway ESP32-S3 com saída ESP-NOW:

```sh
pio run -e esp32-s3-gateway
```

Fazer upload do gateway ESP32-S3:

```sh
pio run -e esp32-s3-gateway -t upload
```

Se mais de uma placa estiver conectada, liste as portas e escolha uma
explicitamente:

```sh
pio device list
pio run -e esp32-s3-gateway -t upload --upload-port COM5
```

Abrir o monitor serial:

```sh
pio device monitor -b 9600
```

## Modo direto RS485 -> display 128x64 SPI

O ambiente `esp32-s3-gateway-lcd-direct` le a bateria Felicity/Felicity ESS pelo
mesmo barramento RS485 do gateway, mas nao usa ESP-NOW. Os dados sao escritos
direto no display grafico ST7565 128x64 e gravados no microSD. Use
`esp32-s3-gateway-lcd-direct-mqtt` para publicar tambem no dashboard remoto.

Compilar:

```sh
pio run -e esp32-s3-gateway-lcd-direct
```

Fazer upload:

```sh
pio run -e esp32-s3-gateway-lcd-direct -t upload
```

Se precisar escolher a porta:

```sh
pio run -e esp32-s3-gateway-lcd-direct -t upload --upload-port COM5
```

Ligacoes padrao do ESP32-S3 neste alvo:

| Funcao | ESP32-S3 |
| --- | --- |
| RS485 RO / RX | GPIO0 |
| RS485 DI / TX | GPIO2 |
| RS485 DE + RE | GPIO1 |
| Display pin 1 `CS` | GPIO15 |
| Display pin 2 `RST` | GPIO17 |
| Display pin 3 `RS (A0)` | GPIO16 |
| Display pin 4 `SCL` / SPI SCK | GPIO4 |
| Display pin 5 `SI` / SPI MOSI | GPIO6 |
| Display pin 6 `VDD` | 3V3 |
| Display pin 7 `GND` | GND |
| Display pin 8 `LEDA` | 3V3 ou VCC do backlight |
| Display pin 9 `LEDK` | GND |
| Display pin 10 `IC_SCK` | nao usar |
| Display pin 11 `IC_CS` | nao usar |
| Display pin 12 `IC_SDO` | nao usar |
| Display pin 13 `IC_SDI` | nao usar |
| microSD CLK / SPI SCK | GPIO4 |
| microSD MOSI | GPIO6 |
| microSD MISO | GPIO5 |
| microSD CS | GPIO7 |
| Botao de pagina | GPIO10 para GND |

Display e microSD agora compartilham o mesmo barramento SPI fisico. O display
usa `SCL` e `SI` com `CS` proprio, enquanto o microSD usa os mesmos `SCK` e
`MOSI`, mais `MISO` e seu proprio `CS`.

Paginas do botao:

| Pagina | Conteudo |
| --- | --- |
| 0 | SOC e potencia com destaque + barra de progresso |
| 1 | Tensao, corrente, potencia e SOC |
| 2 | Comparacao SOC BMS x estimativa LiFePO4 por tensao |
| 3 | Temperatura e status de carga/descarga |
| 4 | Falhas, min/max das celulas e idade do ultimo pacote |

## Modo direto com LoRa

Existem dois ambientes adicionais para enviar as leituras por LoRa em UART
transparente:

```sh
pio run -e esp32-s3-gateway-lcd-direct-lora-e90-dtu
pio run -e esp32-s3-gateway-lcd-direct-lora-e220
```

Por padrao os dois enviam somente o JSON `battery_info` a cada 5 segundos. Para
enviar todos os tipos de `BmsMessage`, altere os flags:

```ini
-D BMS_LORA_BATTERY_INFO_ONLY=0
-D BMS_LORA_MIN_INTERVAL_MS=0
```

Pinagem do `esp32-s3-gateway-lcd-direct-lora-e90-dtu`:

| Funcao | ESP32-S3 |
| --- | --- |
| LoRa RX | GPIO14 |
| LoRa TX | GPIO13 |
| RS485 DE + RE local | GPIO12 |

O E90-DTU tem interface RS232/RS485, entao o ESP32-S3 precisa de MAX3232 para
RS232 ou de um transceiver RS485 para a porta A/B. Se usar RS232, configure
`BMS_LORA_E90_DE_RE_PIN=-1`.

Pinagem do `esp32-s3-gateway-lcd-direct-lora-e220`:

| Funcao | ESP32-S3 |
| --- | --- |
| E220 TXD -> RX | GPIO14 |
| E220 RXD <- TX | GPIO13 |
| E220 M0 | GPIO11 |
| E220 M1 | GPIO12 |
| E220 AUX | GPIO18 |

O firmware coloca M0 e M1 em LOW para operar o E220 em modo normal/transparente
e aguarda o AUX antes/depois do envio quando o pino esta configurado.

Os sketches de teste de bancada ficam em `../lora-tests`. Eles recebem e
enviam texto simples pelo radio e compilam tanto para ESP32 quanto para Arduino
UNO/Nano.

## Teste local das telas

Existe um ambiente separado para testar o display sem bateria ligada. Ele
simula mensagens `BmsMessage` com variacao de tensao, corrente, SOC,
temperatura e falhas para exercitar as cinco paginas do display.

Compilar:

```sh
pio run -e esp32-s3-gateway-display-test
```

Fazer upload:

```sh
pio run -e esp32-s3-gateway-display-test -t upload
```

Abrir o monitor serial:

```sh
pio device monitor -b 9600
```

Nesse modo o firmware nao inicia RS485, microSD nem MQTT. O botao de pagina em
`GPIO10` continua ativo e o monitor serial imprime os valores simulados.

Compilar o teste unitário do pacote:

```sh
pio test -e espnow-packet-test --without-uploading --without-testing
```

Rodar o teste unitário em um ESP32-S3 conectado:

```sh
pio test -e espnow-packet-test
```

O pacote binário ESP-NOW compartilhado com o receptor fica em
`../../shared/espnow_battery_packet.h`. O payload MQTT do modo direto usa o
schema JSON `solmar.bms.reading.v1`.

## Central ESP32-S3 com A7670SA, NEO-6M, LoRa e MQTT

O ambiente `esp32-s3-central-a7670` usa o gateway mais recente como base, mas
sem o display SPI local e sem microSD. Ele le a BMS por RS485, envia leituras
para a frente por ESP-NOW/LoRa, publica no dashboard por MQTT e calcula a
localizacao combinando o GNSS do A7670SA com o NEO-6M.

O A7670SA e a fonte primaria de localizacao. O NEO-6M fica como fallback quando
o fix do A7670SA fica velho ou invalido. A internet usa o A7670SA/4G primeiro
e cai para WiFi quando o 4G nao estiver pronto.

O dashboard assina `solmar/bms/felicity-fla12171/#`. As leituras da BMS saem
em `readings/v1/<device_id>/<tipo>` e a posicao GPS sai em `location/v1`.

OTA por WiFi fica ativo quando a central conecta em uma rede WiFi. O hostname
padrao e `solmar-central` e a senha OTA padrao e `solmar-ota`. O A7670/4G
continua sendo a preferencia para MQTT, mas OTA direto pelo 4G normalmente nao
funciona sem VPN ou rede com rota ate o ESP, porque a operadora usa NAT.

Pinagem inicial configurada:

| Funcao | ESP32-S3 |
| --- | --- |
| RS485 RO / RX | GPIO4 |
| RS485 DI / TX | GPIO5 |
| RS485 DE + RE | GPIO6 |
| A7670SA TX -> ESP RX | GPIO9 |
| A7670SA RX <- ESP TX | GPIO10 |
| A7670SA PWRKEY | GPIO7 |
| NEO-6M TX -> ESP RX software | GPIO15 |
| LoRa E220 TX -> ESP RX | GPIO16 |
| LoRa E220 RX <- ESP TX | GPIO17 |
| LoRa E220 M0 | GPIO18 |
| LoRa E220 M1 | GPIO8 |
| LoRa E220 AUX | GPIO3 |

O NEO-6M usa serial por software a 9600 bps porque os tres UARTs de hardware
ficam ocupados por RS485, A7670SA e LoRa. Se o modulo LoRa for usado com
`M0/M1` fixos em GND, esses dois pinos podem ser liberados depois.

Compilar:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\gateway -e esp32-s3-central-a7670
```

Gravar:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\gateway -e esp32-s3-central-a7670 -t upload
```

Depois dessa primeira gravacao por USB, atualizar por OTA na mesma rede WiFi:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\gateway -e esp32-s3-central-a7670-ota -t upload
```

Se o mDNS `solmar-central.local` nao resolver, informe o IP:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\gateway -e esp32-s3-central-a7670-ota -t upload --upload-port 192.168.1.50
```
