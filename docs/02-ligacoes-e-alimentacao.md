# Ligações e alimentação

Esta página é a que evita fumaça. Cada item aqui é uma decisão tomada, não uma
sugestão.

## O BTS7960 alimentado em 3,3 V — e não em 5 V

**O VCC lógico dos módulos IBT-2 vai em 3,3 V.** Parece errado num módulo
vendido como "5 V", e é a ligação certa.

O motivo está no 74HC244 do módulo. Alimentado em 5 V, ele exige nível alto de
pelo menos **3,5 V** para reconhecer um `1`. O ESP32 entrega 3,3 V. A diferença
é pequena o bastante para o módulo funcionar na bancada fria e falhar depois de
aquecer, ou funcionar num módulo e não em outro do mesmo lote — o tipo de
defeito que se persegue durante horas achando que é software.

Alimentado em 3,3 V, o mesmo 74HC244 passa a exigir cerca de 2,3 V, e o sinal
do ESP fica com folga confortável.

> Isso vale para a **lógica** do módulo. A alimentação de **potência** dos
> motores continua vindo da bateria, no borne grosso.

## O EN, e o que ele não faz

**O EN não controla velocidade.** Velocidade e sentido saem do duty cycle em
RPWM/LPWM; o EN só decide se a ponte existe.

| EN | Estado das saídas | O motor |
| --- | --- | --- |
| **HIGH** | ativas | obedece ao PWM |
| **LOW** | alta impedância | fica **solto**, roda livre |

E há um terceiro estado que não depende do EN: com **EN alto e os dois PWM em
zero**, as duas saídas ficam no GND e o motor **freia**. É isso que o comando
`STOP` faz — e é o que se espera de um comando com esse nome.

`EN 0` e `STOP` são coisas diferentes de propósito: um solta, o outro freia.

### O resistor de 10 k para GND no EN

Obrigatório. Entre o instante em que a placa liga e o instante em que o firmware
configura o pino, o GPIO está em alta impedância — flutuando. Sem o pull-down, a
ponte pode acordar habilitada com o duty ainda indefinido, e o robô dá um tranco
no boot, justamente quando alguém está com ele na mão.

O firmware faz a sua parte na ordem certa (EN em LOW antes de qualquer PWM), mas
firmware só roda depois do boot. O resistor cobre o tempo em que não há firmware
nenhum.

## Alimentação, item por item

| O quê | De onde | Por quê |
| --- | --- | --- |
| **Servos** | regulador próprio, 5–6 V, **≥ 2–3 A** | nunca pelo regulador da SuperMini: um servo travado puxa mais que a placa inteira |
| **MAX98357A** | **5 V** | os picos passam de 1 A |
| **XIAO S3 Sense** | pino **5 V** | câmera + Wi-Fi têm picos altos; alimentar pelo 3V3 derruba a placa no meio de uma captura |
| **Lógica do BTS7960** | **3,3 V** | ver acima |
| **Potência dos motores** | bateria, borne grosso | |

**GND comum entre tudo.** Sem isso, o sinal de PWM não tem referência e a ponte
lê lixo — e o sintoma parece defeito de firmware.

### Capacitor de 470–1000 µF

Perto dos servos **e** perto das pontes. Não é refinamento: a partida de um
motor e o movimento de um servo puxam corrente em degrau, a tensão cai, e o
ESP32 reinicia por brownout. O robô então volta ao boot **no meio do
movimento**, com o log dizendo `rst:0xc (SW_CPU_RESET)` ou simplesmente
recomeçando — e quem estiver olhando vai procurar o defeito no código.

## A UART entre as placas

3,3 V dos dois lados, **ligação direta e cruzada** — sem conversor de nível,
sem resistor.

```
XIAO S3  D6 (GPIO43) TX  ───────►  RX (GPIO20)  ESP32-C3
XIAO S3  D7 (GPIO44) RX  ◄───────  TX (GPIO21)  ESP32-C3
                    GND  ───────   GND
```

O GND dessa ligação é o mesmo GND comum de tudo. Duas placas trocando serial
sem terra comum funcionam por alguns minutos e depois não funcionam mais.

## Checklist antes de energizar a primeira vez

1. **Multímetro na continuidade**: GND de tudo é o mesmo ponto?
2. **VCC lógico das pontes em 3,3 V** — medir, não supor.
3. **Resistor de 10 k entre EN e GND** — presente e medido.
4. **Motores desconectados** no primeiro boot. O robô com as rodas no ar, ou
   melhor ainda, sem as rodas ligadas à ponte.
5. **Fonte de bancada com limite de corrente**, se houver, antes da bateria.
6. Só então energizar, e olhar o console: o corpo anuncia a pinagem e o prazo
   do failsafe no boot.
