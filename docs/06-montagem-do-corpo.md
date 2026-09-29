# Montagem do corpo — passo a passo

**As duas placas já estão gravadas.** Nada de software precisa ser feito depois
da montagem: ligou, funciona. Este documento é só fio, parafuso e conferência.

Siga na ordem. Cada etapa termina num **teste que você faz antes de seguir** —
é o que impede um erro de fiação virar três horas de procura depois.

> **Regra que vale o documento inteiro: os motores só entram no fim.**
> Da etapa 1 à 5 o robô fica sem tração. É de propósito — um erro de fiação
> com motor ligado é um robô saindo da mesa.

---

## O que você vai precisar

| | |
| --- | --- |
| ESP32-C3 SuperMini | o corpo |
| ESP32-CAM | o cérebro |
| 2× BTS7960 (IBT-2) | uma ponte por motor |
| 2× servo | MG90S ou SG90 |
| INMP441 | o microfone |
| **1× resistor 10 kΩ** | EN das pontes → GND |
| **1× resistor 10 kΩ** | GPIO12 da CAM → GND |
| Capacitor 470–1000 µF | perto das pontes e dos servos |
| Regulador 5–6 V, ≥ 2–3 A | só para os servos |
| Fio, e **um GND comum para tudo** | |

---

## Etapa 1 — O GND comum, antes de qualquer outra coisa

Ligue todos os GNDs num ponto só: as duas placas, as duas pontes, o regulador
dos servos, a bateria.

**Confira com o multímetro na continuidade**, encostando uma ponta no GND da C3
e a outra em cada GND do robô. Tem que apitar em todos.

> Duas placas trocando serial sem terra comum funcionam por alguns minutos e
> depois não funcionam mais. E o sintoma parece defeito de firmware.

---

## Etapa 2 — Os dois resistores que não são opcionais

Estes dois resistores existem para cobrir o instante **antes de o firmware
rodar**, quando os pinos ainda estão flutuando. Firmware nenhum resolve isso,
porque firmware nenhum está rodando ainda.

### 2.1 — EN das pontes → GND, 10 kΩ

Ligue **R_EN e L_EN das duas pontes juntos** no **GPIO10** da C3, e ponha um
resistor de 10 kΩ desse mesmo ponto para o GND.

Sem ele, a ponte pode acordar habilitada com o duty ainda indefinido, e o robô
dá um tranco no boot — justamente quando alguém está com ele na mão.

### 2.2 — GPIO12 da ESP32-CAM → GND, 10 kΩ

**Este é o mais importante do documento.** O GPIO12 é o MTDI, e o ESP32 lê esse
pino no boot para decidir a tensão da flash. **Alto no boot = a placa não
inicia**, e fica em laço de reset.

Como o GPIO12 vai receber o sinal do microfone, que flutua antes de o I2S subir,
o pull-down é o que garante que ela ligue **sempre**, e não na maior parte das
vezes.

> Sem ele a placa liga certo algumas vezes e não liga outras. É o pior defeito
> possível de diagnosticar, porque parece intermitência de contato.

**Confira:** multímetro entre GPIO12 e GND, com tudo desligado → ~10 kΩ.

---

## Etapa 3 — O cabo entre as duas placas

Três fios, cruzados:

```
ESP32-CAM  GPIO14 (TX)  ─────────►  GPIO20 (RX)  ESP32-C3
ESP32-CAM  GPIO13 (RX)  ◄─────────  GPIO21 (TX)  ESP32-C3
                   GND  ──────────  GND
```

Sem conversor de nível e sem resistor: são 3,3 V dos dois lados.

> **Cruzado.** TX de um vai no RX do outro. Ligar reto não queima nada e não
> funciona nunca.

### Teste da etapa 3 — e é o teste que vale mais de todo o documento

Energize **só as duas placas**, sem motores e sem servos. Abra o console do
cérebro (a ESP32-CAM, pelo adaptador USB, 115200) e digite `?`.

```
corpo:       respondendo
             46 enviados, 46 respostas, 0 recusas
```

| O que aparece | O que significa |
| --- | --- |
| `corpo: respondendo`, enviados ≈ respostas | **cabo certo, siga** |
| `corpo: MUDO` | um dos dois fios está errado, ou o GND não é comum |
| `recusas` maior que zero | fio certo, ruído na linha — conferir GND |

**Agora puxe o fio do TX com o robô "andando"** (digite `w`, e sem motores nada
se move). Em **um segundo** o corpo deve registrar o failsafe no próprio
console, e o cérebro deve começar a dizer `corpo MUDO`.

Esse é o ensaio que valida o failsafe de verdade. O resto é teoria.

---

## Etapa 4 — O microfone

INMP441 na ESP32-CAM:

| INMP441 | ESP32-CAM |
| --- | --- |
| VDD | 3,3 V |
| GND | GND |
| **L/R** | **GND** — canal esquerdo, que é o que o firmware lê |
| SCK | GPIO15 |
| WS | GPIO2 |
| SD | GPIO12 *(o do resistor da etapa 2.2)* |

### Teste da etapa 4

No console, digite `?` e olhe a linha do microfone. Fale perto dele e digite `?`
de novo.

```
microfone:   ok  pico recente 0.031 em 556 blocos
```

O **pico** tem que subir quando você fala. Se ficar em `0.000` com blocos
contando, o I2S está rodando e o microfone não está mandando nada — confira o
L/R no GND e a alimentação em 3,3 V.

Se aparecer `microfone sem sinal - conferir a ligacao do INMP441`, é literalmente
isso.

---

## Etapa 5 — Os servos

| Servo | Pino da C3 |
| --- | --- |
| Servo 1 | GPIO5 |
| Servo 2 | GPIO6 |

**A alimentação dos servos vem do regulador próprio de 5–6 V, nunca da placa.**
Um servo travado puxa mais corrente que a C3 inteira. Capacitor de 470–1000 µF
perto deles.

### Teste da etapa 5

No console do cérebro, `1` e `2` alternam cada servo entre 45° e 135°.

**Olhe o curso mecânico.** Se o braço bater no batente antes de chegar, **pare** e
anote o ângulo: ele vira limite em `include/config_corpo.h`, e nunca mais se
discute. Servo empurrando batente não para — esquenta e queima, e o barulho é o
único aviso.

---

## Etapa 6 — As pontes e os motores, por último

### 6.1 Sinal

| BTS7960 | C3 |
| --- | --- |
| Motor esquerdo RPWM / LPWM | GPIO0 / GPIO1 |
| Motor direito RPWM / LPWM | GPIO3 / GPIO4 |
| R_EN + L_EN das duas | GPIO10 *(já com o 10 k da etapa 2.1)* |

### 6.2 Alimentação — o item que mais dá errado

**VCC lógico dos módulos em 3,3 V, não em 5 V.** Parece errado num módulo
vendido como "5 V", e é a ligação certa: o 74HC244 do módulo, alimentado em 5 V,
exige nível alto de ≥ 3,5 V, e o ESP32 entrega 3,3 V. Funciona na bancada fria e
falha depois de aquecer.

A potência dos motores continua vindo da bateria, no borne grosso.

**Meça o VCC lógico antes de energizar.** Não suponha.

### 6.3 Primeiro acionamento — com as rodas no ar

Suspenda o robô. As rodas **não** podem tocar a mesa.

No console: `w` (frente), `x` (parar), `s` (ré), `a` e `d` (girar).

| Observar | Se der errado |
| --- | --- |
| As duas rodas giram no mesmo sentido com `w` | um motor está com RPWM/LPWM trocados |
| Nenhum reinício da placa | é brownout: capacitor e fonte dos servos |
| `x` para na hora | |

### 6.4 No chão

Só agora. Bateria carregada, espaço livre à frente, e digite `t` — a rotina
completa: frente 40% por 1,5 s, parar, girar no lugar, mexer os servos.

**O número a anotar é o duty mínimo de partida** — abaixo de certo valor o motor
só zumbe e não gira. Previsto entre 15% e 25%. Ele define a velocidade útil
mínima do robô e não dá para calcular, só medir. Vai para o
[diário](../diario.md), com previsto ao lado de medido.

---

## Cartão de bolso — o console do cérebro

Adaptador USB na ESP32-CAM, 115200 baud.

| Tecla | |
| --- | --- |
| `?` | estado de tudo: corpo, microfone, câmera, memória |
| `w a s d x` | frente, esquerda, ré, direita, **parar** |
| `1` `2` | alterna cada servo entre 45° e 135° |
| `t` | rotina de teste completa |
| `f` | mede um quadro da câmera |
| `p` | despeja a foto em base64 pela serial |
| `h` | a ajuda de novo |

**O robô não anda sozinho ao ligar.** Nada se move até você mandar. A placa
costuma ser ligada com o robô na mesa, e mesa tem borda.

---

## Se alguma coisa não funcionar

| Sintoma | Causa mais provável |
| --- | --- |
| A CAM não liga, ou reinicia sem parar | **o 10 k do GPIO12** (etapa 2.2) |
| `corpo: MUDO` | TX/RX não cruzados, ou GND não comum |
| A placa reinicia quando o motor parte | brownout: capacitor e fonte |
| O motor só zumbe | duty abaixo do mínimo de partida — é normal, é o número do ensaio 6.4 |
| Um motor gira ao contrário | RPWM/LPWM trocados naquele lado |
| `microfone sem sinal` | L/R do INMP441 não está no GND |
| A ponte funciona fria e falha quente | VCC lógico em 5 V em vez de 3,3 V |

---

## A primeira foto desta câmera

![A primeira foto da ESP32-CAM, 320x240, 29/09/2026](../evidencias/marcos/primeira-foto-20260929.jpg)

Tirada em 29/09/2026 com a placa ainda solta na bancada, antes de existir robô.
São 11 860 bytes de JPEG, 320×240, que atravessaram o cabo de gravação em base64
porque esta placa **não tem cartão SD** — e não tem porque os seis pinos do
cartão são exatamente os que a UART e o microfone ocupam.

A primeira tentativa saiu quase preta, com 5087 bytes. O conserto não foi
iluminar o quarto: foi liberar o teto de ganho do OV2640 de 16× para 128× e
descartar doze quadros antes de capturar, porque o controle automático leva
vários quadros para convergir — e mais ainda em luz fraca. Os dois ajustes estão
em [`src/camera.h`](../src/camera.h) e valem para o robô todo, que vai viver em
sombra de móvel e corredor.
