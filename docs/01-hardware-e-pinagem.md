# Hardware e pinagem

Duas placas, e a divisão entre elas é a decisão de arquitetura do projeto.

## Por que duas placas

O corpo não tem PSRAM, não processa áudio e não usa Wi-Fi: só movimento. E
reconhecimento de voz local só existe na S3 (ESP-SR) — por isso o cérebro é ela.

| | XIAO ESP32-S3 Sense | ESP32 DevKit |
| --- | --- | --- |
| Papel | **cérebro** | **corpo** |
| Núcleos | 2 × 240 MHz | 2 × 240 MHz |
| PSRAM | 8 MB octal | nenhuma |
| Cuida de | câmera, microfone, alto-falante, Wi-Fi, decisão | motores, servos, controle de PS4 |
| Wi-Fi | sim | **não usa** |
| Bluetooth | BLE | **clássico** — o que o controle de PS4 fala |

**Por que a DevKit, e não mais o C3** (desde 05/10/2026): o DualShock 4 fala
Bluetooth clássico, e o ESP32 clássico é o único da família que ainda o tem. O
C3 e a S3 só fazem BLE.

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

## ESP32 DevKit — o corpo

| Função | Pino |
| --- | --- |
| Motor esquerdo RPWM / LPWM | **GPIO32** / **GPIO33** |
| Motor direito RPWM / LPWM | **GPIO25** / **GPIO26** |
| EN (R_EN + L_EN das duas pontes) | **GPIO27**, com 10 k para GND |
| Servo 1 / servo 2 | **GPIO19** / **GPIO18** |
| UART1 RX ← cérebro | **GPIO16** (marcado RX2) |
| UART1 TX → cérebro | **GPIO17** (marcado TX2) |
| Controle de PS4 | Bluetooth clássico, nenhum pino |

Escolhidos para caber em poucos pedaços de header: as pontes em **cinco pinos
seguidos** de um lado (32, 33, 25, 26, 27), servos e enlace lado a lado do
outro. **Nenhum é de *strapping*** (0, 2, 5, 12, 15), e o console fica no UART0,
que é o USB da placa — log e enlace nunca dividem fio.

Sobram 10 dos 16 canais de PWM.

## ESP32-C3 SuperMini — o corpo até 05/10/2026, reserva

Mesmo firmware, ambiente `corpo_c3`, sem o controle de PS4.

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

## Os seis canais de PWM do C3, todos ocupados (só no C3)

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

---

## A ESP32-CAM como cérebro — o plano B

Há **uma única XIAO ESP32-S3 Sense** no laboratório, e três projetos a querem.
A ESP32-CAM destravou este aqui enquanto a Sense estava no FarmIO; desde
01/10/2026 a Sense é o cérebro e a CAM ficou de reserva. Ela é pior, e funciona.

### A aritmética de pinos que torna isso possível

A ESP32-CAM expõe 10 pinos, e a câmera já comeu o resto por dentro. Sobram:

```
GPIO1, GPIO3                          UART0 - console e gravação
GPIO2, GPIO4, GPIO12, GPIO13, GPIO14, GPIO15
GPIO16                                NÃO: é o CS da PSRAM
```

Os seis do meio **são os pinos do cartão SD**. Com cartão em 4 bits sobram dois;
sem cartão, sobram seis — e foi por isso que **este projeto não usa cartão SD**.
Foi a decisão que fez a placa caber.

| Função | Pino | Ressalva |
| --- | --- | --- |
| UART2 → corpo | TX **GPIO14**, RX **GPIO13** | os dois melhores: nenhum é strapping |
| INMP441 BCLK | **GPIO15** | strap MTDO: alto no boot só suprime o log da ROM |
| INMP441 WS | **GPIO2** | strap, seguro como saída |
| INMP441 DATA | **GPIO12** | **exige 10 k para o GND — ver abaixo** |
| livre | GPIO4 | o LED de flash, e o DIN do amplificador quando ele chegar |

O UART do corpo vai no **UART2**, e não no UART0: esta placa não tem USB nativo,
então perder o UART0 seria perder o único jeito de falar com ela.

### O resistor de 10 k no GPIO12 não é opcional

O GPIO12 é o **MTDI**, e o ESP32 lê esse pino no boot para decidir a tensão da
flash. Alto no boot significa flash em 1,8 V, e **a placa não inicia** — fica em
laço de reset até o pino baixar.

O INMP441 só dirige o SD dentro do próprio slot; antes de o firmware configurar
o I2S, a linha flutua. O pull-down cobre exatamente essa janela. Sem ele a placa
liga certo algumas vezes e não liga outras, que é o pior defeito possível de
diagnosticar.

### A câmera do ESP32 clássico é um periférico I2S

Isto não estava em nenhum datasheet que eu li, e apareceu na primeira vez que a
placa ligou:

```
E (132) intr_alloc: No free interrupt inputs for I2S0 interrupt
E (133) camera: Camera config failed with error 0xffffffff
```

**No ESP32 clássico a interface DVP da câmera roda sobre o I2S0**, em modo
paralelo, e nenhum outro controlador serve. Duas consequências, as duas já no
firmware:

1. **A câmera inicializa antes do microfone**, para ficar com o I2S0. Na XIAO S3
   a ordem é indiferente — lá a câmera tem controlador próprio —, então a mesma
   sequência serve nas duas placas.
2. **O microfone é fixado no I2S1** com `setPort(I2S_NUM_1)`. A alocação
   automática não resolve sozinha: a câmera usa o driver I2S *antigo* e o
   microfone usa o *novo*, e o novo continua achando que o I2S0 está livre.

### O que se perde em relação à Sense

1. **Não roda ESP-SR.** Reconhecimento de palavra local só existe no S3. Isso
   derruba a recomendação de deixar *"para"* funcionando sem rede — e essa era a
   única ordem que não deveria depender de Wi-Fi. A alternativa é um botão
   físico de parada: mais confiável, e inútil quando o robô já está longe da mão.
2. **Sem cartão SD**, por decisão de pinos. Gravar foto ou áudio passa a exigir
   rede.
3. **Sem USB nativo.** Grava pela UART0, com o GPIO0 no GND — que é também o
   XCLK da câmera, embora o conflito seja só no boot.
4. **4 MB de PSRAM** em vez de 8.

Trocar de volta é tirar um flag: `-DCEREBRO_CAM`. O resto do firmware é o mesmo
arquivo.
