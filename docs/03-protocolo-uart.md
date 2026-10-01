# O protocolo entre o cérebro e o corpo

Texto ASCII, 115200 8N1, **um comando por linha**, terminado em `\n`. O `\r` é
ignorado — terminal de Windows manda CRLF, e ninguém quer descobrir isso às duas
da manhã.

A especificação executável é [`include/protocolo.h`](../include/protocolo.h),
compilado nas **duas** placas. Protocolo escrito duas vezes é protocolo que
diverge: um lado passa a acreditar num timeout que o outro não tem.

## Por que texto, e não binário

As duas pontas precisam ser depuráveis com um terminal serial e um teclado. Num
robô que anda, poder abrir o monitor e digitar `M 40 40` sem nenhum software do
lado do PC vale mais do que os bytes economizados. O corpo aceita comandos tanto
pelo enlace quanto pelo próprio console USB, e é o mesmo parser nos dois.

## Comandos

| Comando | Efeito |
| --- | --- |
| `M <esq> <dir>` | velocidade dos motores, −100 a 100 (`M 70 -70` gira no lugar) |
| `S <n> <ângulo>` | servo `n` (1 ou 2) no ângulo 0–180 |
| `STOP` | freia os dois motores |
| `EN <0/1>` | solta / habilita as pontes (0 também zera o PWM) |
| `PING` | responde `PONG` |

Aceita minúscula e espaço sobrando. Não aceita comando pela metade: uma linha
maior que 64 caracteres é **descartada inteira**, com `ERR linha-longa`.

Esse descarte é deliberado. Truncar seria pior que recusar: `M 100 100` cortado
vira `M 100 10`, que é um comando **válido e errado** — o robô andaria torto por
causa de um byte perdido.

## Respostas

| Resposta | Quando |
| --- | --- |
| `OK` | comando aceito |
| `ERR <motivo>` | recusado — `comando-desconhecido`, `argumentos`, `fora-de-faixa`, `linha-longa` |
| `PONG` | resposta ao `PING` |

O motivo é uma palavra só, sem espaço: o cérebro compara com `strcmp` e o humano
lê no monitor sem precisar de tabela.

## O failsafe

**É a parte que importa deste documento.**

Um robô com motor de tração que perde o cérebro não para sozinho: o último `M`
continua valendo e ele sai andando até bater em alguma coisa ou cair da mesa.

Por isso **quem para não é o cérebro — é o corpo**, contando o tempo desde o
último comando de movimento.

```
cérebro reenvia a cada  250 ms   (protocolo::HEARTBEAT_MS)
corpo  desiste com     1000 ms   (protocolo::SILENCIO_MS)
```

A folga de 4× existe porque o cérebro tem Wi-Fi, câmera e áudio disputando dois
núcleos, e um atraso de 200 ms ali é normal. Folga menor faria o robô **tremer**
— parar e voltar a andar — por jitter de escalonamento, e tremer parece defeito
elétrico.

### O que alimenta o relógio

| Comando | Alimenta? |
| --- | --- |
| `M`, `STOP`, `EN`, `PING` | **sim** |
| `S` (servo) | **não** |
| linha recusada, linha vazia | **não** |

O servo ficar de fora é a regra que dá sentido ao resto: um robô andando em
linha reta que só mexe a cabeça continua sendo um robô andando. Se o enlace de
movimento morreu, ele tem que parar mesmo que os servos ainda estejam
obedecendo.

`M 0 0` **alimenta**, e isso também é de propósito. Parado por vontade é
diferente de parado por abandono: o cérebro dizendo "continuo aqui, e a minha
intenção é ficar parado" não pode virar log de failsafe.

### As duas metades não confiam uma na outra

- Se o **cérebro** travar, o corpo para em 1 s.
- Se a **task de heartbeat** morrer sem derrubar o resto do cérebro, o corpo
  para em 1 s.
- Se o **corpo** reiniciar, o cérebro vê o silêncio de `PONG` e registra
  `corpo MUDO` no log.

Nenhum dos dois lados precisa que o outro esteja correto para se comportar bem.

## O que passa pelo enlace e não é movimento

A ESP32-CAM não tem USB, e o adaptador dela ocupa o header inteiro: com ele
encaixado não há microfone nem enlace. Com o robô montado, **o único USB que
sobra é o do corpo** — então o enlace carrega também o console do cérebro.

| Linha | Sentido | Efeito |
| --- | --- | --- |
| `#<texto>` | cérebro → corpo | relato: o corpo mostra no USB como `[cam] <texto>`, não responde e não alimenta o failsafe |
| `><texto>` | USB → corpo → cérebro | o resto da linha vale como digitado no console do cérebro (`>g`, `>?`) |
| `PONTE` | só no USB do corpo | liga o USB direto ao enlace, byte a byte |

`PONTE` existe para gravar firmware no cérebro sem o adaptador
([`atualiza_serial.h`](../src/atualiza_serial.h),
[`scripts/grava_pelo_enlace.py`](../scripts/grava_pelo_enlace.py)). Enquanto ela
está aberta o corpo **não interpreta nada** — um bloco de firmware pode conter
`M 100 100` por acaso —, e por isso as pontes H ficam **soltas** do começo ao
fim. Fecha sozinha depois de 5 s sem byte vindo do USB.

Nenhuma das três muda o parser de movimento: `#` e `>` são desviadas antes de
ele ser chamado, e `PONTE` só é aceita pelo console USB, nunca pelo enlace.

## Quatro propostas — decisão do Henrique

O prompt do projeto pede para **discutir antes de implementar**. Nenhuma destas
está no código; ficam aqui com a recomendação e o custo.

### 1. Rampa de aceleração — recomendo ligar

Já está **escrita e desligada** em `RAMPA_SUBIDA_MS = 0`
([`include/config_corpo.h`](../include/config_corpo.h)). Com zero, `M` vale na
hora, exatamente como o protocolo diz.

A favor: mandar um motor parado direto para 100 % puxa a corrente de rotor
travado, que num motor pequeno chega a várias vezes a nominal. Com servo e ponte
na mesma fonte, esse pico é o caminho mais curto para um brownout — o robô
reinicia no meio do movimento e ninguém entende por quê.

Contra: rampa atrasa a reação. Por isso, na forma escrita, ela **sobe devagar e
desce na hora** — proteger a fonte não vale atrasar uma parada.

Valor sugerido: **250 ms** de 0 a 100 %. Uma linha para ligar.

### 2. `STATUS` — recomendo, é barato

Um comando que devolve estado em uma linha: velocidade atual dos dois lados,
ângulo dos servos, EN, uptime, quantas vezes o failsafe disparou. O corpo já
conta tudo isso para o log; falta só expor.

Vale porque hoje esses números só existem no console USB do corpo — que é
justamente o cabo que não está ligado quando o robô está andando pela sala.

### 3. Checksum — não recomendo agora

Um XOR no fim da linha custa pouco. Mas o enlace é de **20 cm, 3,3 V, dentro do
mesmo chassi**, e a 115200 baud. Se houver corrupção aí, ela vem de ruído das
pontes ou de GND mal feito — e o remédio certo é o cabo, não o protocolo.
Checksum nesse cenário esconderia o defeito em vez de mostrá-lo.

Fica para quando o enlace sair do chassi ou a velocidade subir.

### 4. ID de sequência — não recomendo

Resolve duplicata e reordenação, que a UART não produz. Numa serial ponto a
ponto, o que chega, chega em ordem. Seria complexidade sem defeito
correspondente.
