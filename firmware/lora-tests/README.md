# Testes simples dos pares LoRa

Esta pasta tem sketches de bancada para testar os pares sem depender do BMS.
Use o mesmo sketch nos dois lados do par correspondente. Abra o monitor serial
dos dois lados: o sketch imprime tudo que recebe pelo radio e tambem envia
pings automaticos.

Projetos:

- `e90-dtu-basic-test`: teste para o par E90-DTU.
- `e220-900t22d-basic-test`: teste para o par E220-900T22D.
- `e220-c3-sender`: sender simples para ESP32-C3 no E220.
- `e220-uno-receiver-hwserial`: receiver simples para Arduino UNO usando pinos 0/1.

## E90-DTU

O E90-DTU nao liga direto nos pinos TTL do ESP32/Arduino. Use a porta RS485 do
DTU com um transceiver RS485 no microcontrolador, ou use a porta RS232 do DTU
com um conversor MAX3232.

Padrao do sketch em RS485:

| Sinal local | ESP32 | Arduino UNO/Nano |
| --- | --- | --- |
| RX do microcontrolador | GPIO16 | D10 |
| TX do microcontrolador | GPIO17 | D11 |
| DE + RE do transceiver RS485 | GPIO4 | D4 |

Se usar RS232/MAX3232 em vez de RS485, compile com:

```ini
build_flags =
	-D LORA_USE_RS485_DRIVER=0
```

## E220-900T22D

O E220 usa UART TTL. Ligue GND comum, alimente conforme o seu modulo e mantenha
a antena conectada antes de transmitir.

### Teste simples pedido: ESP32-C3 sender e Arduino UNO receiver

Use este par quando o ESP32-C3 esta no primeiro E220 e o Arduino UNO esta no
segundo E220.

ESP32-C3 sender:

| E220 | ESP32-C3 |
| --- | --- |
| TXD | GPIO20 |
| RXD | GPIO21 |
| GND | GND |
| VCC | alimentacao do modulo |
| M0 | GND |
| M1 | GND |

Arduino UNO receiver usando UART hardware:

| E220 | Arduino UNO |
| --- | --- |
| TXD | D0 / RX |
| RXD | D1 / TX |
| GND | GND |
| VCC | alimentacao do modulo |
| M0 | GND |
| M1 | GND |

No Arduino UNO, D0/D1 sao a mesma UART usada pelo USB. Para gravar o firmware,
desconecte o E220 dos pinos D0/D1; depois do upload, reconecte o E220. O LED
13 pisca quando o Arduino recebe uma linha. O receiver tambem devolve um `UNO
receiver ok ...` pelo LoRa, entao esse retorno aparece no monitor serial do
ESP32-C3.

Upload:

```sh
cd firmware/lora-tests/e220-c3-sender
pio run -e esp32-c3-devkitm -t upload --upload-port COM_DO_ESP

cd ../e220-uno-receiver-hwserial
pio run -e uno -t upload --upload-port COM_DO_ARDUINO
```

Monitor do ESP32-C3:

```sh
cd firmware/lora-tests/e220-c3-sender
pio device monitor -p COM_DO_ESP -b 115200
```

O ESP imprime `TX> ESP32C3 E220 sender ...` a cada 3 segundos. Quando o Arduino
receber e responder, o monitor do ESP tambem mostra `UNO receiver ok ...`.

Para o primeiro teste com o seu par E220, use:

- ESP32-C3: sender.
- Arduino UNO/Nano: receiver.

Pinos definidos no `platformio.ini`:

| Sinal E220 | ESP32-C3 sender | ESP32/S3 sender | Arduino UNO/Nano receiver |
| --- | --- | --- | --- |
| TXD do E220 -> RX do microcontrolador | GPIO20 | GPIO16 | D10 |
| RXD do E220 <- TX do microcontrolador | GPIO21 | GPIO17 | D11 |
| M0 | GPIO4 | GPIO4 | D7 |
| M1 | GPIO5 | GPIO5 | D6 |
| AUX | GPIO6 | GPIO18 | D5 |

M0 e M1 ficam em LOW para modo normal/transparente. Se preferir amarrar M0 e
M1 direto no GND, deixe os pinos desconectados no codigo usando `-1`.

Upload sugerido:

```sh
cd firmware/lora-tests/e220-900t22d-basic-test
pio run -e esp32-c3-devkitm -t upload --upload-port COM_DO_ESP
pio run -e uno -t upload --upload-port COM_DO_ARDUINO
```

No monitor serial do ESP aparece `Role: ESP sender`; no Arduino aparece
`Role: Arduino Uno receiver`. O ESP manda pings automaticos e o Arduino imprime
as linhas recebidas como `RX> ...`.

Se o ESP mostra `TX> ESP-C3-SENDER ...` e o Arduino mostra apenas
`RX idle: nenhum pacote recebido ainda`, o firmware ja esta rodando dos dois
lados. Confira a bancada nesta ordem:

- GND comum entre microcontrolador e E220 em cada lado.
- E220 alimentado em tensao correta e com corrente suficiente.
- Antena conectada nos dois E220.
- TXD do E220 cruzado para RX do microcontrolador, e RXD do E220 cruzado para TX.
- M0 e M1 realmente em LOW nos dois modulos.
- Parametros internos iguais nos dois E220: baud serial, canal/frequencia,
  taxa de ar, endereco e modo transparente.

## Compilar com PlatformIO

Cada projeto de teste tem ambientes para ESP32, ESP32-S3 e Arduino UNO:

```sh
cd firmware/lora-tests/e90-dtu-basic-test
pio run -e esp32dev
pio run -e esp32-s3-devkitm
pio run -e uno
```

```sh
cd firmware/lora-tests/e220-900t22d-basic-test
pio run -e esp32dev
pio run -e esp32-s3-devkitm
pio run -e uno
```

Antes do teste, configure os dois modulos do par com a mesma frequencia, taxa
de ar, modo transparente, canal/endereco e baud serial. O codigo de teste nao
altera os parametros internos do radio.
