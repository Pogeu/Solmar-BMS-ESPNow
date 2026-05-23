# Firmware Gateway

Lê os valores do BMS Felicity/Felicity ESS via RS485 Modbus e transmite os
dados principais da bateria por ESP-NOW.

Este firmware intencionalmente não publica por MQTT e não conecta em uma rede
WiFi. O ESP-NOW usa o rádio do ESP32 diretamente, sem SSID, senha, roteador ou
broker MQTT.

Compilar o gateway ESP32-C3:

```sh
pio run -e esp32-c3-gateway
```

Fazer upload do gateway ESP32-C3:

```sh
pio run -e esp32-c3-gateway -t upload
```

Se mais de uma placa estiver conectada, liste as portas e escolha uma
explicitamente:

```sh
pio device list
pio run -e esp32-c3-gateway -t upload --upload-port COM5
```

Abrir o monitor serial:

```sh
pio device monitor -b 9600
```

## Modo direto RS485 -> LCD 16x2

O terceiro firmware deste projeto e o `esp32-c3-gateway-lcd-direct`. Ele le a
bateria Felicity/Felicity ESS pelo mesmo barramento RS485 do gateway, mas nao
usa ESP-NOW. Os dados sao escritos direto no LCD I2C 16x2.

Compilar:

```sh
pio run -e esp32-c3-gateway-lcd-direct
```

Fazer upload:

```sh
pio run -e esp32-c3-gateway-lcd-direct -t upload
```

Se precisar escolher a porta:

```sh
pio run -e esp32-c3-gateway-lcd-direct -t upload --upload-port COM5
```

Ligacoes padrao do ESP32-C3 neste alvo:

| Funcao | ESP32-C3 |
| --- | --- |
| RS485 RO / RX | GPIO0 |
| RS485 DI / TX | GPIO2 |
| RS485 DE + RE | GPIO1 |
| LCD SDA | GPIO8 |
| LCD SCL | GPIO9 |
| Botao de pagina | GPIO4 para GND |

O LCD usa endereco I2C `0x27` por padrao. Se o seu modulo estiver em outro
endereco, ajuste `LCD_I2C_ADDR` no `platformio.ini`.

Paginas do botao:

| Pagina | Conteudo |
| --- | --- |
| 0 | SOC e potencia em numeros grandes |
| 1 | Tensao, corrente, potencia e SOC |
| 2 | Comparacao SOC BMS x estimativa LiFePO4 por tensao |
| 3 | Temperatura e status de carga/descarga |
| 4 | Falhas, min/max das celulas e idade do ultimo pacote |

Compilar o teste unitário do pacote:

```sh
pio test -e espnow-packet-test --without-uploading --without-testing
```

Rodar o teste unitário em um ESP32-C3 conectado:

```sh
pio test -e espnow-packet-test
```

O pacote binário ESP-NOW compartilhado com o receptor fica em
`../../shared/espnow_battery_packet.h`.
