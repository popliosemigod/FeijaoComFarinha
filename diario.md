# Diário de bancada — Feijão com Farinha

Uma entrada por iteração, com o **previsto ao lado do medido**. Enquanto não
houver ensaio, o que entra aqui é decisão de projeto — e o que ela custou.

---

## 2026-09-28 — O repositório nasce com as duas placas compilando

Projeto novo, a partir do caderno de arquitetura que o Henrique escreveu: XIAO
ESP32-S3 Sense como cérebro, ESP32-C3 SuperMini como corpo, ponte H BTS7960,
dois servos, MAX98357A.

### O que foi escrito

| | |
| --- | --- |
| `include/protocolo.h` | o combinado, compilado nas **duas** placas |
| `include/config_corpo.h` / `config_cerebro.h` | pinagem e parâmetros, sem lógica |
| `src/motores.h` | as duas pontes, com o caminho até o hardware fechado |
| `src/servos.h` | LEDC direto, sem biblioteca |
| `src/linha.h` | juntar bytes até virar linha, sem bloquear |
| `src/corpo_link.h` | o lado do cérebro, com heartbeat em task própria |
| `src/main_corpo.cpp` | o corpo inteiro, com failsafe |
| `src/main_cerebro.cpp` | enlace, sentidos, console e a rotina de teste |
| `src/main_autoteste.cpp` | a lógica das duas, numa placa nua |

### Três decisões que valem registro

**1. Um projeto PlatformIO, não dois.** O caderno sugeria `/corpo-c3/` e
`/cerebro-s3/` como projetos separados. Um só, com dois ambientes, foi
preferido por um motivo concreto: o `protocolo.h` é compilado nas duas placas.
Separar obrigaria a copiá-lo, e protocolo copiado é protocolo que diverge — um
lado passa a acreditar num timeout que o outro não tem. É também o formato dos
outros projetos do laboratório, e faz `pio run` compilar as duas placas de uma
vez, inclusive no CI.

**2. O robô não anda sozinho ao ligar.** A rotina de teste do caderno existe,
mas só começa quando alguém digita `t` no console. A placa costuma ser ligada
com o robô na mesa, e mesa tem borda.

**3. A rampa de aceleração ficou escrita e desligada.** O caderno pede para
discutir antes de implementar. Com `RAMPA_SUBIDA_MS = 0`, o comando vale na
hora, exatamente como o protocolo promete; ligar é trocar uma constante. Na
forma escrita ela **sobe devagar e desce na hora** — proteger a fonte não vale
atrasar uma parada.

### O erro que custou mais tempo, e não era do firmware

A primeira compilação falhou com:

```
Error: Failed to install Python dependencies into penv
```

Mensagem que não diz nada. Com `-v`, o que apareceu embaixo:

```
FileNotFoundError: [Errno 2] No such file or directory:
'C:\Users\henri\.platformio\.cache\tmp\pkg-installing-d913c_m1\esp32-arduino-libs\
 esp32c5\include\espressif__esp_matter\connectedhomeip\connectedhomeip\src\app\
 clusters\camera-av-settings-user-level-management-server\
 CameraAvSettingsUserLevelManagementCluster.h'
```

**Esse caminho tem exatamente 260 caracteres** — o `MAX_PATH` do Windows, no
número redondo. Não é dependência de Python nenhuma: é o pacote do core 3.x
trazendo headers do Matter para o ESP32-C5, com nomes longos, e o Windows
cortando na extração.

Duas saídas. `LongPathsEnabled = 1` no registro resolve, mas é máquina inteira
e pede administrador. Encurtar o diretório do PlatformIO resolve sem isso:

```powershell
$env:PLATFORMIO_CORE_DIR = "C:\pio"
```

De `C:\Users\henri\.platformio` (26 caracteres) para `C:\pio` (6) sobram 20
caracteres, e 240 < 260. A conta é essa, e é por isso que funciona.

Registrado no README porque **vai acontecer de novo** — na máquina dele, ou na
próxima vez que este repositório for clonado num caminho fundo. A mensagem
original não deixa ninguém descobrir isso sozinho.

### O que não está provado

Nada foi ligado. O firmware compila; nenhum motor girou, nenhum servo se mexeu,
nenhum quadro foi capturado. Os limites de ângulo são de projeto, e o duty
mínimo de partida dos motores não existe até alguém medir.

Roteiro dos seis ensaios, com as previsões escritas antes, em
[`docs/04-roteiro-de-bancada.md`](docs/04-roteiro-de-bancada.md).

---

## 2026-09-29 — As placas na mesa, e três defeitos que só o hardware mostrou

Henrique tem **uma única XIAO ESP32-S3 Sense**, e três projetos a querem. A
decisão foi tentar a ESP32-CAM como cérebro. Ela é pior, e roda — e o caminho
de volta ficou aberto num flag de compilação.

As duas placas apareceram no PC: **COM7 = ESP32-C3** (o corpo) e
**COM6 = ESP32 clássico** (a CAM), identificadas pelo esptool.

### Ensaio 0, feito: 56 de 56

O autoteste deixou de ser promessa. Gravado no C3, ele roda em **4 ms**.

Na primeira execução: **53 verificações, 52 passaram, 1 falhou.**

```
[NAO] subida em passos  (obtido 100, esperado 10)
```

`aproxima(0, 100, 10)` devolvia 100. A guarda de inversão de sentido estava
escrita `(alvo > 0) != (atual > 0)`, e **zero não é positivo**: partir do
repouso era classificado como inversão, e a rampa inteira era pulada.

Exatamente no único momento para o qual a rampa existe — o pico de corrente da
partida. Ligar `RAMPA_SUBIDA_MS` teria "funcionado" sem fazer absolutamente
nada, e o brownout continuaria aparecendo.

Corrigido para exigir que os dois tenham sinal e sinais opostos. Quatro casos
novos entraram no autoteste, incluindo o que escapou. Agora: **56 de 56**.

Vale o registro do método: o defeito estava numa expressão booleana de uma
linha, revisada por mim duas vezes, e num ramo de código **desligado**. Quem o
encontrou foi um teste rodando num microcontrolador de verdade, antes de
existir um motor.

### A câmera do ESP32 clássico é um periférico I2S

Primeiro boot da CAM:

```
E (132) intr_alloc: No free interrupt inputs for I2S0 interrupt
E (133) camera: Camera config failed with error 0xffffffff
```

A mensagem não fala em câmera. A causa: **no ESP32 clássico a interface DVP da
câmera roda sobre o I2S0**, em modo paralelo, e só o I2S0 serve. O microfone
subia antes no `setup()` e levava o I2S0 embora.

Conserto um: a câmera inicializa primeiro. Na XIAO S3 a ordem é indiferente,
porque lá a câmera tem controlador próprio — então a mesma sequência serve nas
duas placas.

Só que o microfone continuou falhando:

```
E (206) i2s_common: Allocate rx dma channel failed
```

Conserto dois: **fixar o microfone no I2S1** com `setPort()`. A alocação
automática não percebe o conflito porque a câmera usa o driver I2S *antigo* e o
microfone usa o *novo* — e o novo continua achando que o I2S0 está livre.

(E `I2S_NUM_1`, não o literal `1`: no ESP32 clássico `i2s_port_t` é um enum de
verdade, e inteiro não compila.)

### O terceiro: o log que se enterra sozinho

Com o microfone de pé mas sem INMP441 ligado, cada leitura devolvia timeout — e
o driver reclama sozinho, vinte vezes por segundo, enterrando qualquer outra
mensagem. Depois de um segundo mudo, a task passa a tentar a cada 2 s e avisa
uma vez. O diagnóstico continua; o console volta a ser legível.

### O que ficou medido

| | |
| --- | --- |
| Autoteste no C3 | 56 de 56, em 4 ms |
| Quadro JPEG da OV2640 em QVGA | **5103 bytes** |
| PSRAM livre | 4063 KB de 4096 KB |
| Heap livre, com câmera e I2S de pé | 240 KB |
| Flash / RAM do `cerebro_cam` | 13,1% / 11,0% |
| Heartbeat sem o corpo | 46 enviados, 0 respostas, e o log acusa `corpo MUDO` |

### O que continua sem prova

Nenhum motor girou. O INMP441 não está ligado — o nível medido é zero porque
não há microfone nos pinos, não porque ele esteja mudo. E **as duas placas
nunca conversaram**: falta o cabo da UART entre elas, e o `corpo MUDO` no log é
o comportamento correto, não um defeito.

---

## 2026-09-29 (2) — A primeira foto, e o failsafe medido em 1001 ms

Henrique pediu para deixar tudo armado antes de desmontar a bancada: as duas
placas gravadas, a montagem documentada, e a primeira foto publicada.

### O failsafe deixou de ser promessa

Gravando o firmware do corpo no C3 e digitando comandos direto no console dele,
o ensaio 1 aconteceu sem precisar de mais nada:

| | Previsto | Medido |
| --- | --- | --- |
| Failsafe dispara | entre 1000 e 1010 ms | **1001 ms** |
| `M 200 0` | `ERR fora-de-faixa`, sem mover | **exatamente isso** |
| `PING` alimenta o relógio | sim | **sim** — `cerebro voltou` |
| `S 1 45` alimenta o relógio | **não** | **não** — o failsafe disparou 1001 ms depois |

A última linha é a regra que dá sentido ao projeto inteiro: *um robô andando em
linha reta que só mexe a cabeça continua sendo um robô andando*. Estava escrita
no `protocolo.h`, verificada no autoteste, e agora está medida na placa.

### A primeira foto custou duas tentativas

A ESP32-CAM não tem cartão SD — pelos pinos, que a UART e o microfone ocupam — e
o Wi-Fi ainda não está configurado. Então a imagem saiu **em base64 pela própria
serial de gravação**, entre marcadores, com o comando `p` no console.

Base64 custa um terço a mais que binário e atravessa um terminal sem que nenhum
byte seja lido como controle. Num link que também carrega log em texto, binário
cru se perderia.

**Primeira tentativa: 5087 bytes, quase preta.** Dava para ver que era a
bancada, e só.

O conserto não foi acender a luz. O OV2640 sobe com teto de ganho de 16× e
controle automático ainda convergindo; em luz fraca isso entrega quadro escuro.
Liberando o teto para 128× e descartando doze quadros antes de capturar:

```
---FOTO-INICIO 11860 320 240---     ffd8ff ... ffd9
```

**11 860 bytes**, mais que o dobro — e mais bytes de JPEG na mesma resolução
significa literalmente mais detalhe sobrevivendo à compressão. A imagem está em
`evidencias/marcos/`.

Os dois ajustes ficaram no firmware, e não no procedimento: um robô de sala vive
em sombra de móvel e corredor, então pouca luz é o caso normal, não a exceção.

### Uma decisão de repositório

`evidencias/` é ignorado pelo git — enche de captura repetida e pesada. Mas
**a primeira foto de uma câmera não se repete**, e etapa provada por um arquivo
que não está no repositório não está provada para ninguém.

Então `evidencias/marcos/` passa a ser versionado, por exceção explícita no
`.gitignore`. Entram poucas imagens, escolhidas, e cada uma é citada em
documento.

### O que ficou armado

As duas placas estão gravadas com o firmware final: `corpo` no ESP32-C3,
`cerebro_cam` na ESP32-CAM. Depois da montagem não há nada de software a fazer —
ligou, funciona. O passo a passo está em
[`docs/06-montagem-do-corpo.md`](docs/06-montagem-do-corpo.md), com um teste ao
fim de cada etapa e os motores deixados deliberadamente para o final.
