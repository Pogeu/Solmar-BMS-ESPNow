# CYD battery display

Firmware para a ESP32-2432S028R / Cheap Yellow Display usada como tela local do
projeto Solmar BMS. A placa recebe da central ESP32-S3 por ESP-NOW e mostra uma
tela de leitura de bateria com atualizacao parcial para reduzir flicker.

## O que a tela mostra

- SOC da bateria em destaque.
- Potencia, tensao, corrente e temperatura.
- Barra de alarme: sem alarmes, falha, bateria baixa, dado velho ou comunicacao
  intermitente.
- Rodape com estado de RS485, 4G e LoRa.
- Painel inferior alternavel por touch ou pelo botao BOOT.

Paineis inferiores:

| Pagina | Conteudo |
|---|---|
| 0 | Resumo: tensao, corrente e potencia |
| 1 | Comunicacao: 4G, SD, LoRa e RS485 |
| 2 | Celulas: menor tensao, maior tensao e quantidade de celulas |
| 3 | GPS/enlace: satelites, pacote, sequencia e ID da BMS |

## Pinagem configurada

O display ILI9341 usa o SPI principal da placa. O touch XPT2046 usa outro SPI
para nao conflitar com a tela.

| Funcao | ESP32 |
|---|---|
| TFT MISO | GPIO12 |
| TFT MOSI | GPIO13 |
| TFT SCLK | GPIO14 |
| TFT CS | GPIO15 |
| TFT DC | GPIO2 |
| TFT RST | -1 |
| TFT BL | GPIO21 |
| Touch CS | GPIO33 |
| Touch CLK | GPIO25 |
| Touch MISO / T_DO | GPIO39 |
| Touch MOSI / T_DIN | GPIO32 |
| Touch IRQ | GPIO36 |
| Botao BOOT / pagina | GPIO0 |

O touch foi implementado com leitura direta do XPT2046 em `HSPI`. A troca de
pagina considera o `T_IRQ` e a pressao lida por SPI.

## ESP-NOW

O ambiente `esp32-cyd-battery-screen` espera pacotes `EspNowBatteryPacket`
definidos em `../../shared/espnow_battery_packet.h`.

O canal inicial e configurado por:

```ini
-D ESP_NOW_WIFI_CHANNEL=1
```

Quando a tela fica sem pacotes, ela varre os canais WiFi 1 a 13 ate encontrar a
central. Isso ajuda quando o canal real do WiFi muda.

## Build

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\cyd-display -e esp32-cyd-battery-screen
```

## Upload por USB

Liste as portas:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe device list
```

Grave na porta da CYD:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\cyd-display -e esp32-cyd-battery-screen -t upload --upload-port COM8
```

## Demo sem central

Use o ambiente demo para validar visual, touch e troca de paginas sem a central
ligada:

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe run -d firmware\cyd-display -e esp32-cyd-battery-screen-demo -t upload --upload-port COM8
```

## Monitor serial

```powershell
C:\Users\pedro\.platformio\penv\Scripts\platformio.exe device monitor -p COM8 -b 115200
```

Mensagens importantes:

```text
Touch XPT2046 ready CS=33 CLK=25 MISO=39 MOSI=32 IRQ=36 Z_MIN=350
ESP-NOW receiver ready, scanning from channel 1
CYD battery screen ESP-NOW receiver started
Touch page=N
```

Se o botao BOOT trocar pagina mas o touch nao, confira primeiro a revisao da
placa. Algumas CYD clones mudam a pinagem do touch; nesse caso ajuste
`TOUCH_CS`, `TOUCH_SCLK_PIN`, `TOUCH_MISO_PIN`, `TOUCH_MOSI_PIN` e
`TOUCH_IRQ_PIN` no `platformio.ini`.
