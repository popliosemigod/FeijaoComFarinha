# Hardware e pinagem

Duas placas, e a divisão entre elas é a decisão de arquitetura do projeto.

## Por que duas placas

O ESP32-C3 SuperMini tem **um núcleo RISC-V a 160 MHz, ~400 KB de RAM e nenhuma
PSRAM**. Ele faria áudio no limite — decodificação junto com HTTPS e Wi-Fi
disputando o único núcleo — e reconhecimento de voz local nele é impossível: o
ESP-SR só roda na S3.

| | XIAO ESP32-S3 Sense | ESP32-C3 SuperMini |
| --- | --- | --- |
| Papel | **cérebro** | **corpo** |
| Núcleos | 2 × 240 MHz | 1 × 160 MHz |
| PSRAM | 8 MB octal | nenhuma |
| Cuida de | câmera, microfone, alto-falante, Wi-Fi, decisão | motores, servos |
| Wi-Fi | sim | **não usa** |

O corpo não usar Wi-Fi é a metade que protege: o pior caso de qualquer defeito
no cérebro — travar numa rede ruim, ficar sem memória decodificando um quadro —
é o robô **parar**, nunca sair andando sem comando.

## XIAO ESP32-S3 Sense — o cérebro

| Função | Pino | Observação |
| --- | --- | --- |
| Microfone PDM | CLK **GPIO42**, DATA **GPIO41** | embutido na placa Sense, I2S0 em modo PDM RX |
| Câmera | conector próprio | pinos internos, tabela abaixo |
| MAX98357A BCLK | D0 = **GPIO1** | I2S1, TX |
| MAX98357A LRC | D1 = **GPIO2** | |
| MAX98357A DIN | D2 = **GPIO3** | VIN em 5 V, SD solto (liga sozinho) |
| UART TX → corpo | D6 = **GPIO43** | vai ao RX do corpo (GPIO20) |
| UART RX ← corpo | D7 = **GPIO44** | vem do TX do corpo (GPIO21) |
| Livres | D3, D4, D5 | mais D8, D9, D10 se o cartão SD não for usado |

O microfone (I2S0) e o amplificador (I2S1) usam **periféricos separados**, e por
isso podem ter taxas de amostragem diferentes e funcionar ao mesmo tempo. Isso
não é detalhe: um robô que precisa ouvir *"para!"* no meio da própria frase
depende exatamente disso.

### Pinos da câmera (roteados pela Seeed, não são escolha)

```
PWDN  -1     RESET -1     XCLK  10     SIOD  40     SIOC  39
Y9    48     Y8    11     Y7    12     Y6    14     Y5    16
Y4    18     Y3    17     Y2    15
VSYNC 38     HREF  47     PCLK  13
```

Estão escritos em `include/config_cerebro.h` em vez de virem de um cabeçalho de
terceiro, para que uma atualização de biblioteca não mude a pinagem em silêncio.

## ESP32-C3 SuperMini — o corpo

| Função | Pino |
| --- | --- |
| Motor esquerdo RPWM / LPWM | **GPIO0** / **GPIO1** |
| Motor direito RPWM / LPWM | **GPIO3** / **GPIO4** |
| EN (R_EN + L_EN das duas pontes) | **GPIO10**, com 10 k para GND |
| Servo 1 | **GPIO5** |
| Servo 2 | **GPIO6** |
| UART1 RX ← cérebro | **GPIO20** |
| UART1 TX → cérebro | **GPIO21** |

Livres: GPIO2, GPIO7, GPIO8, GPIO9 — todos de *strapping*/boot, só usar com
cargas que não interfiram no boot. **Não usar GPIO18/19**, que são o USB.

### A armadilha do GPIO20/21

São os pinos do **UART0**, o console padrão da placa. O enlace só pode morar
neles porque o log de depuração sai pelo **USB CDC**
(`ARDUINO_USB_CDC_ON_BOOT=1` no `platformio.ini`).

Compilar sem CDC põe o log e o enlace no mesmo fio: o cérebro passa a receber
texto de boot no lugar de `OK`, e o sintoma parece ruído elétrico — cabo,
malha, aterramento — quando é uma flag de compilação. Vale o mesmo para o
D6/D7 do cérebro, que são o UART0 dele.

## Os seis canais de PWM do C3, todos ocupados

| Uso | Canais | Frequência | Resolução |
| --- | --- | --- | --- |
| Motores (4 PWM) | 4 | 20 kHz | 10 bits |
| Servos | 2 | 50 Hz | 14 bits |

**Não há canal livre.** Qualquer PWM novo — buzzer, LED com brilho, fita
endereçável por LEDC — exige tirar outro daqui primeiro.

Os 20 kHz ficam acima da audição: em 1 kHz a ponte canta, e num robô de sala
isso incomoda mais do que parece. Os 14 bits do servo são o **máximo do LEDC do
C3**, e dão passo de 1,2 µs; com 10 bits o passo seria de 19,5 µs, cerca de 1,7°,
visível como tranco em movimento lento.
