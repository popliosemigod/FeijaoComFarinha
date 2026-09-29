# Roteiro de bancada

As previsões abaixo foram escritas **antes** de qualquer ensaio. É o que separa
medir de confirmar o que já se acreditava: quando o número sair diferente, a
diferença estará registrada em vez de esquecida.

Cada ensaio vai para o [diário](../diario.md) com o **previsto ao lado do
medido**.

---

## Ensaio 0 — o autoteste, antes de existir robô  ✅ FEITO (29/09)

**Não precisa de robô nenhum.** Grava em qualquer ESP32 da mesa.

```powershell
pio run -e autoteste -t upload
pio device monitor -b 115200
```

**Previsto:** 50 verificações, 50 passam, em menos de 100 ms.

**Medido:** 53 verificações, **52 passaram e 1 falhou** — a rampa era pulada
na partida do repouso. Corrigido, mais quatro casos novos: **56 de 56, em 4 ms**.

O que ele prova: parser (linha suja, CRLF, minúscula, fora de faixa), a regra do
failsafe, o leitor de linha (linha partida, linha longa), a montagem de comando
do lado do cérebro, a conta de pulso do servo e a rampa.

O que ele **não** prova: nada elétrico. Nenhum motor, nenhum servo, nenhuma
corrente.

---

## Ensaio 1 — o corpo sozinho, sem motor ligado  ✅ FEITO (29/09), menos o duty

Só a C3 energizada, **pontes desconectadas dos motores**, console USB aberto.

Digitar no monitor: `PING`, `M 40 40`, `S 1 45`, `STOP`, `EN 0`, `M 300 0`.

| Previsto | |
| --- | --- |
| `PING` responde `PONG` | |
| `M 300 0` responde `ERR fora-de-faixa` | e **não** move nada |
| Sem digitar nada por 1 s, aparece `FAILSAFE` no log | uma vez, não repetido |
| Osciloscópio/multímetro em RPWM: duty proporcional | 40 % pedidos ≈ 40 % medidos |

**A medida que importa:** o tempo entre o último comando e a linha de failsafe.
Previsto entre 1000 e 1010 ms. **Medido: 1001 ms**, nas duas vezes.

`M 200 0` respondeu `ERR fora-de-faixa` sem mover nada, e `S 1 45` **não**
alimentou o relógio — o failsafe disparou 1001 ms depois dele. Falta só a
medida de duty no osciloscópio, que precisa do instrumento na bancada.

---

## Ensaio 2 — os servos

Servos com alimentação **própria**, GND comum, robô ainda desmontado.

| Previsto | |
| --- | --- |
| `S 1 0` → 500 µs de pulso | ±20 µs |
| `S 1 90` → 1450 µs | |
| `S 1 180` → 2400 µs | |
| Nenhum servo zumbe parado | se zumbir, o batente mecânico é menor que 0–180 |

**Se algum servo bater no batente antes de 0 ou de 180, o limite entra em
`config_corpo.h` e nunca mais se discute.** Servo empurrando batente não para:
esquenta e queima, e o barulho é o único aviso.

---

## Ensaio 3 — as duas placas conversando

Cabo da UART ligado, motores ainda desconectados.

```powershell
pio run -e cerebro -t upload
pio device monitor -b 115200     # no cérebro
```

Digitar `?` no console do cérebro.

| Previsto | |
| --- | --- |
| `corpo: respondendo` | e não `MUDO` |
| `enviados ≈ respostas` | diferença de no máximo 1 |
| `recusas: 0` | qualquer recusa aqui é comando montado errado |

**Depois:** puxar o fio TX do cérebro com o robô "andando" (rodas no ar). O
corpo deve parar em 1 s, e o cérebro deve começar a registrar `corpo MUDO`.
Esse é o ensaio que realmente valida o failsafe — o resto é teoria.

---

## Ensaio 4 — o robô no chão, pela primeira vez

Motores conectados. **Bateria carregada** e espaço livre à frente.

Digitar `t` no console do cérebro: frente 40 % por 1,5 s, parar, girar no lugar
1 s, mexer os servos.

| Previsto | O que observar |
| --- | --- |
| Anda reto | se puxar para um lado, é diferença entre os motores — vira número de calibração |
| Nenhum reinício | reinício aqui é brownout: capacitor e fonte dos servos |
| Duty mínimo de partida | previsto entre 15 % e 25 % — abaixo disso o motor só zumbe |

O número mais útil deste ensaio é o **duty mínimo de partida**. Ele define o
piso de velocidade útil do robô, e não dá para calcular — só medir.

---

## Ensaio 5 — os sentidos

Com o robô parado e o console aberto:

| Tecla | Previsto |
| --- | --- |
| `b` | bipe audível de 150 ms no MAX98357A |
| `f` | quadro JPEG QVGA entre 8 KB e 25 KB |
| `?` | `pico recente` do microfone sobe ao falar perto |

O microfone não grava nada nesta etapa, e é de propósito: até o serviço de voz
ser escolhido ([05-a-voz.md](05-a-voz.md)), o que existe é a captura e o nível.
Nível que sobe quando alguém fala já prova o caminho inteiro do PDM.

---

## Ensaio 6 — autonomia

Robô andando em quadrado, bateria medida a cada 5 minutos, até a primeira falha.

Não há previsão: nem a bateria nem o consumo real dos motores estão definidos.
Este ensaio **cria** o número em vez de confirmar um.
