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

---

## 2026-09-29 (3) — Um console pela rede, e uma pergunta sobre a placa nua

Henrique perguntou duas coisas: se dá para tirar a parte de trás da ESP32-CAM
(o adaptador com o conector micro USB) e alimentar a placa direto, e como
controlar o robô para testes.

A segunda respondeu a primeira: **hoje não existe nenhum caminho de rede no
firmware.** Controle era só pela serial — e a serial é justamente o que some se
o adaptador USB sair. Construir o console por Wi-Fi era o que faltava para as
duas perguntas terem a mesma resposta boa: "sim, pode tirar, e ainda dá para
testar sem ele".

### O console por rede

`src/console_rede.h`: um `WiFiServer` na porta 23 (telnet, sem nada para
instalar), um cliente por vez, texto simples. A decisão de projeto que importa:
**é a MESMA função `executaComando(Print&, char)` que a serial chama** — não
existe "modo remoto" tratado à parte, que é justamente o tipo de duplicação que
diverge em silêncio sem ninguém notar.

Sem senha no protocolo, pela mesma razão que a ponte do Jaspy roda em claro na
LAN: quem já está na rede de casa já controla o robô pela mesma porta que o
cérebro usa para tudo o mais. Só ativa se `WIFI_SSID` estiver preenchido —
sem credencial, o robô sobe do mesmo jeito, só sem esse console.

### A interrupção que virou lição de honestidade

No meio da implementação as duas placas desapareceram das portas COM —
Henrique já estava desmontando a bancada. O firmware com o console novo
**compila limpo**, mas nunca foi regravado na ESP32-CAM.

Isso quebraria uma afirmação que eu tinha feito horas antes: "as placas já
estão gravadas com o firmware final". Não estão mais — o `cerebro_cam` na placa
é de um commit anterior. A correção foi no README, não escondida: uma seção
inteira agora diz exatamente isso, e pede a regravação **antes** de tirar o
adaptador USB de vez, porque depois disso não há outro jeito de gravar sem
desmontar de novo.

### Dois hooks bloqueados, e por que não é alarme

`detect-private-key` e `gitleaks` falharam com `[WinError 4551] Uma política de
Controle de Aplicativo bloqueou este arquivo` — o Windows impedindo o binário
de rodar, não achando segredo nenhum. Conferido à mão: nenhuma credencial real
entrou no diff, só os placeholders de sempre em `secrets.example.h`.

---

## 2026-09-29 — Reconhecimento de objetos: o outro lado do `p`

Henrique pediu, no mesmo dia da montagem: a câmera já tira foto, falta ela
"reconhecer" o que vê. E pediu especificamente uma base **já rotulada**, vinda
da internet — não treinar do zero.

`scripts/reconhece.py` fecha o laço que `camera.despeja()` deixou aberto: manda
`p` (serial ou rede — a mesma função do console já serve as duas), decodifica o
base64, salva o `.jpg` em `evidencias/reconhecimento/` e classifica com
**MobileNetV2 pré-treinado em ImageNet** (mil categorias, via `torchvision`,
baixado uma vez e cacheado). Cada captura vira uma linha em `catalogo.csv` — o
começo de um banco de imagens rotuladas do robô, sem precisar treinar nada
para existir.

**Decisão de arquitetura, não implementação:** a inferência roda no PC, nunca
na ESP32-CAM. MobileNetV2 não cabe no orçamento de RAM de uma placa que já
reparte PSRAM entre câmera e microfone — documentado em
[`docs/07-reconhecimento-de-objetos.md`](docs/07-reconhecimento-de-objetos.md).

**Smoke test, sem hardware.** Rodado contra a foto real já salva em
`evidencias/marcos/primeira-foto-20260929.jpg`, para provar o caminho de
código (baixar pesos, decodificar, montar tensor, classificar) antes de a
placa estar ligada de novo:

```
11860 bytes lidos
  2.6%  golfcart
  2.0%  harvester
  1.9%  forklift
```

Confiança baixa é esperada — a foto original saiu escura, e ImageNet não tem
categoria "bancada de eletrônica". O que este teste prova é que o script
funciona, não que o reconhecimento é bom; isso só se mede mostrando um objeto
de verdade à câmera.

**Achado, não corrigido:** `camera.despeja()` e `tarefaSentidos()` escrevem no
mesmo `Serial` a partir de tarefas diferentes. Uma foto em curso pode ter uma
linha de log do microfone ("sem sinal") interleaved no meio do base64 — o
script detecta pelo tamanho não bater, mas não recupera. Fica registrado em
[`docs/07`](docs/07-reconhecimento-de-objetos.md#limitações-honestas) como
risco de arquitetura do console, não deste script.

**O que não está provado:** a captura pela serial de verdade — as placas
seguem desconectadas desde a sessão da montagem. Só o caminho de classificação
foi exercitado nesta entrada.

---

## 2026-09-29 — A foto no celular, e por que "baixar" e não só "mostrar"

No meio da sessão anterior, Henrique decidiu: vai testar sempre pelo celular
(um Galaxy A14), e pediu para as fotos e vídeos "de alguma forma" chegarem na
galeria do aparelho — com liberdade para escolher como.

O console de rede já existia (`console_rede.h`), mas é telnet: texto puro,
sem imagem. `src/web_cerebro.h` abre uma segunda porta (80), só para foto —
`http://<ip>/` no navegador do celular, com "Tirar foto" e, depois da
primeira captura, "Baixar".

**A decisão que resolve "galeria" sem escrever nenhum código de Android:** o
atributo `download` num `<a>` faz o navegador salvar o arquivo pela via normal
de downloads, e a galeria de fábrica (Android e Samsung) já indexa a pasta
Download sozinha. Não é integração com a galeria — é servir o arquivo do jeito
que a galeria já sabe achar.

**Reuso deliberado.** `Camera::captura()` (novo método em `camera.h`) copia o
buffer do driver antes de devolvê-lo, e `WebCerebro::serveFoto()` manda esse
buffer direto pelo `WebServer::client()`, sem passar por `String` — é
literalmente o mesmo desenho que `main_cam.cpp` do FarmIO já usa (`g_cop`,
`capturaFresca()`), só do lado do cérebro em vez da câmera do vaso.

`scripts/reconhece.py` ganhou `--http`: pede a mesma foto que o celular já
viu, em JPEG binário — sem base64, e sem o risco de corrida no console
documentado na entrada anterior.

**Provado:** `pio run` compila limpo nas quatro placas (`corpo`, `cerebro`,
`cerebro_cam`, `autoteste`) com o arquivo novo.

**Não provado:** nenhum celular abriu a página ainda. `serveFoto()` e
`pedeFoto()` seguem o padrão já em produção no FarmIO, mas isso é
"dimensionado", não "testado" — falta o ensaio de verdade.

---

## 2026-09-30 — O teste simples: som → motor, e a fiação que não está lá

Henrique ligou as duas placas no PC (C3 no COM7, a CAM no COM6 pelo FTDI) e
pediu um teste básico só com três peças: as duas placas, o INMP441 e os motores
nas pontes H. O resto fica de fora por agora.

### O que foi escrito

`src/main_teste_som.cpp`, ambiente `teste_som`: a CAM só com microfone e
enlace. A lógica é uma frase — **um som alto faz o robô andar 1,5 s a 40 % e
parar**. O corpo roda o `corpo` normal, sem mudança, com o failsafe valendo.

O console mostra as três peças separadas (nível dos dois canais do I2S, corpo
respondendo ou mudo, estado), e duas teclas tiram o microfone do caminho:
`g` anda agora, `x` para.

### Medido

| | Previsto | Medido |
| --- | --- | --- |
| Gravar `corpo` no C3 | ok | ok, COM7 |
| Gravar `teste_som` na CAM | pede jumper no GPIO0 | **gravou sozinho**: o FTDI faz o auto-reset |
| Enlace CAM → corpo | `M 0 0` a cada 250 ms | **nada chega**: o corpo cai em failsafe 1 s depois de qualquer `PING` manual |
| Enlace corpo → CAM | `PONG` de volta | **0 bytes**: 53 `PONG` emitidos, nenhum visto no GPIO13 nem no GPIO14 |
| INMP441 | nível > 0 no canal esquerdo | **só zeros** nos dois canais, e em todas as 12 combinações de BCLK/WS/SD |

Nenhum dos dois defeitos é de firmware. O corpo responde `PING` pelo USB, a CAM
envia e conta; o que falta é cobre entre as duas, e entre a CAM e o microfone.
**Nenhum motor girou** — de propósito: sem enlace o som não chega neles, e
girar pelo console do corpo sem ninguém olhando o robô não prova nada.

### Dois diagnósticos que ficaram no firmware de teste

**O enlace, sem ambiguidade.** Enlace mudo não diz qual fio falhou. A primeira
tentativa foi medir a tensão de repouso (um TX fica em nível alto) — e ela
**mentiu**: o GPIO13 leu 3,1 V sem estar ligado em TX nenhum, porque os pinos do
cartão SD da ESP32-CAM têm pull-up na placa. O que ficou: se o `PING` não volta,
a CAM **só escuta**, um pino de cada vez, enquanto alguém manda `PING` no USB do
corpo. O `PONG` aparece no pino em que o TX do corpo realmente está — e se for o
pino errado, o teste sobe o enlace trocado e avisa (`Corpo::begin()` ganhou os
pinos como parâmetro opcional, só para isso).

**O microfone, tecla `v`.** Varre BCLK/WS/SD pelos pinos livres e classifica a
linha de dados: sinal, só zeros, presa em 1. Sinal de verdade mexe nos 24 bits
de cima; só o último bit mudando é linha solta pegando a borda do relógio
vizinho — a primeira versão da varredura chamou isso de "SINAL" e estava errada.

### A máquina, que quase impediu tudo

`pio run` não compilava nesta máquina, por dois motivos que não aparecem no CI:

- **Caminho longo.** O pacote de bibliotecas do core 3.x tem um arquivo cujo
  caminho, no diretório temporário do PlatformIO, passa de 260 caracteres.
  Resolvido com `PLATFORMIO_CACHE_DIR=C:\Users\henri\.pc`; o definitivo é ligar
  `LongPathsEnabled` no Windows (pede administrador).
- **Git Bash.** O instalador dos toolchains recusa rodar sob MSYS
  (`MSys/Mingw is not supported`) e o erro seguinte é `g++ não é reconhecido`.
  Rodar o `pio` pelo PowerShell.

### O que continua sem prova

Motor girando, e o microfone ouvindo. Os dois dependem de fio: GPIO14 da CAM →
GPIO20 do C3, GPIO13 da CAM ← GPIO21 do C3, e o INMP441 em 3,3 V / GND / 15 /
2 / 12. A CAM está com o `teste_som` gravado — o `cerebro_cam` (câmera, foto)
volta com `pio run -e cerebro_cam -t upload --upload-port COM6`.

---

## 2026-09-30 (2) — O adaptador não é o caminho: o enlace passa a carregar tudo

Henrique religou os fios e o enlace **CAM → corpo passou a funcionar**: o corpo
parou de cair em failsafe. Só que eu pedi para ele ligar o adaptador USB da CAM
"com os fios como estão" para ler o microfone, e a resposta dele foi o achado da
sessão: **o adaptador ocupa o header inteiro. Com ele encaixado não há
microfone nem enlace.** Ler a CAM pelo USB dela e testar o robô montado são
coisas que não acontecem ao mesmo tempo.

Com o robô montado, o único USB que sobra é o do corpo. Então o enlace passou a
carregar o que faltava.

### O que foi escrito

| | |
| --- | --- |
| `#texto` (cérebro → corpo) | relato; o corpo mostra no USB como `[cam] texto` |
| `>texto` (USB → corpo → cérebro) | vale como digitado no console do cérebro |
| `PONTE` (USB do corpo) | USB ligado cru ao enlace, com as pontes H soltas |
| `src/atualiza_serial.h` | o cérebro recebe firmware novo por um `Stream` e grava na outra partição |
| `scripts/grava_pelo_enlace.py` | o lado do PC: blocos com offset e CRC, um por vez, com resposta |
| `board_build.partitions = min_spiffs.csv` | duas partições de aplicação na CAM — a `huge_app` padrão tem uma só |

O `teste_som` ganhou o console pelo enlace (`>g`, `>x`, `>?`), `d` para
desarmar o som (mede sem mover o robô), `+`/`-` para o limiar, e o relato do
microfone a cada 500 ms pelo fio.

### Medido

| | Previsto | Medido |
| --- | --- | --- |
| `PONTE` no C3 | bytes da CAM crus no USB | **ok**: `M 0 0` a cada 250 ms, `M 40 40` × 6, `STOP` |
| Som → motor, firmware antigo na CAM | dispara com palma | **dispara sozinho, em laço**: `STOP` a cada 3,0 s exatos |
| O mesmo, com as pontes H soltas pela `PONTE` | igual | ciclo de **4,0 s**: o disparo vem ~1 s depois do descanso, não na hora |

O laço é defeito meu, não do microfone: o ruído de fundo só era aprendido
quando **não** havia disparo, então um nível constante acima do limiar dispara
para sempre. Corrigido (o descanso também aprende o fundo), compilado, **não
gravado**. A diferença entre 3,0 s e 4,0 s diz que as pontes habilitadas mudam
o que o microfone ouve — motor girando, ou ruído elétrico delas na linha de
dados. Qual dos dois, só os números do microfone dizem, e eles só saem pelo
firmware novo.

### O que não está provado, e o que custa provar

**Nada do lado da CAM rodou.** O firmware que está nela é o de antes — sem
relato, sem console pelo enlace, sem receptor de gravação — e ele só sai de lá
pelo UART0. Ou seja: **falta uma última gravação pelo adaptador**, com os fios
fora. Depois dela, `scripts/grava_pelo_enlace.py` troca o firmware pelo COM7.

Também sem prova: o sentido corpo → CAM do fio (o `r` do relato vai dizer), e o
receptor de gravação em si. Com a CAM no adaptador dá para provar o receptor
antes de remontar: `python scripts/grava_pelo_enlace.py --direto --porta COM6`.

---

## 2026-10-01 — A XIAO S3 Sense volta a ser o cérebro

Henrique perguntou se trocar a ESP32-CAM pela XIAO S3 Sense deixaria o projeto
mais simples. A resposta foi sim, e ele tirou a placa do FarmIO. Os três
problemas que travaram a bancada no dia anterior são da CAM, não do projeto:
sem USB (o adaptador ocupa o header), microfone externo com cinco fios e um
resistor que decide se a placa liga, e câmera e microfone disputando o I2S0.

### O que mudou no repositório

O caminho de volta estava aberto desde o começo — `cerebro` é o ambiente da
XIAO e sempre compilou. Faltava o teste simples, que tinha um `#error` para
qualquer placa que não fosse a CAM:

- `main_teste_som.cpp` roda nas duas. A diferença inteira é o microfone: PDM
  mono de 16 bits na XIAO, INMP441 estéreo de 32 na CAM. A varredura de pinos
  só existe na CAM — na XIAO não há pino a varrer.
- `teste_som` passou a ser o da XIAO; o da CAM virou `teste_som_cam`.

### Medido — a primeira vez que este firmware roda numa XIAO

| | Previsto | Medido |
| --- | --- | --- |
| `cerebro` sobe sem ajuste | sim | **sim**: câmera ok, microfone ok, alto-falante ok |
| PSRAM | 8 MB | **8156 KB livres de 8192 KB** |
| Heap livre, com câmera e I2S de pé | — | **280 KB** |
| Foto pela serial (`p`) | JPEG íntegro | **4 271 bytes**, 320×240, `ffd8ff … ffd9` |
| Microfone PDM, sala em silêncio | acima de zero | **0,0005 a 0,002** |
| Microfone PDM, voz perto da placa | — | **0,01 a 0,055** |
| Som → disparo | dispara e não repete em laço | **2 disparos em 30 s**, com o fundo subindo de 0,004 durante a fala |
| Enlace com o corpo | `MUDO` sem os fios | **`MUDO`**, 0 respostas — o C3 não estava ligado |

O disparo em laço do dia anterior não voltou: com o descanso aprendendo o ruído
de fundo, fala contínua dispara uma vez e para de disparar.

A foto saiu desfocada e de perto — a placa estava solta na bancada. Fica em
`evidencias/xiao/`, **fora do git**: mostra parte de um rosto, e o repositório é
público.

### O que custou tempo, e não era da placa

**A XIAO não enumerava no USB** (`Dispositivo USB Desconhecido — falha na
solicitação de descritor`). Eu culpei o firmware do FarmIO ("deve dormir") sem
ter olhado — e ele não dorme nem desliga o USB. Era a porta ou o cabo: na outra
porta do PC ela apareceu estável.

**Meu gravador falhou 116 vezes seguidas** com a placa já funcionando. O
`esptool` chamado com a saída redirecionada quebra ao imprimir a barra de
progresso (`UnicodeEncodeError`, cp1252), **depois de conectar e antes de
gravar**. `PYTHONUTF8=1` resolve; o `pio run -t upload` nunca teve o problema.

### O que continua sem prova

Motor girando, e a XIAO conversando com o C3: faltam os três fios (D6 → GPIO20,
D7 → GPIO21, GND). O limiar de 0,02 dispara com voz normal perto da placa —
para uma palma ele provavelmente está baixo, e isso só se acerta com o robô
montado, porque o motor também faz barulho.

---

## 2026-10-01 (2) — Fala para texto: a placa recorta, o PC transcreve

Depois do teste do microfone, Henrique perguntou o que tinha sido gravado e o
que ele tinha dito. A resposta honesta era "nada": o `teste_som` só mede volume.
E ele pediu o que faltava — reconhecimento de voz, fala para texto.

A decisão que o `docs/05` deixava em aberto saiu pelo caminho que já era o
recomendado: Whisper rodando na máquina do laboratório, o mesmo motor
(`faster-whisper`) da ponte do Jaspy, que já estava instalado e com o modelo
`small` em cache. Pelo cabo USB, porque o Wi-Fi do robô ainda não tem credencial.

### O que foi escrito

| | |
| --- | --- |
| `src/voz_serial.h` | `VozPorSerial`: recorta cada frase (começo, fim, 300 ms de pré-rolagem) e despeja em base64 |
| `main_cerebro.cpp` | teclas `o` / `q` ligam e desligam a escuta; a task do microfone parou de dormir entre blocos |
| `scripts/ouve.py` | junta o áudio, transcreve, salva o `.wav` e a linha em `evidencias/voz/` |

A placa não transcreve nada. Ela faz a metade que só ela pode fazer: decidir
onde a frase começa e acaba.

**Um defeito antigo, que só o áudio mostrou.** A task dos sentidos lia 16 ms de
som e dormia 20 ms: consumia menos da metade do que o microfone produzia. Para
medir nível não fazia diferença — e por isso passou; para gravar uma frase,
picotava tudo. A leitura já bloqueia até o bloco chegar, então a espera saiu.

### Medido

Sem ninguém falando na sala, o teste foi com a voz sintética do Windows
(Maria, pt-BR) pelos alto-falantes do notebook, a cerca de um metro da placa.

| Dito | Transcrito |
| --- | --- |
| Feijão com farinha, ande para a frente. | `Feijão com farinha, ande para a frente.` |
| Pare agora. | `Para e agora.` |
| Vire para a esquerda e tire uma foto. | `Vire para a esquerda e tira uma foto.` |
| Olá, eu sou o feijão com farinha. | `Olá, eu sou o Feijão com farinha.` |
| Que horas são agora? | `O que horas são agora?` |

| | Previsto | Medido |
| --- | --- | --- |
| Blocos do microfone por segundo | 62,5 | **~66** — a task não perde mais áudio |
| Frases recortadas, primeira versão | 3 de 3 | **1 de 3** |
| Frases recortadas, depois do conserto | — | **5 de 5** |
| Tempo para transcrever (CPU, `small`) | alguns segundos | **2,5 a 3,6 s**; uma levou 8,9 s |

**Por que 1 de 3.** O ruído de fundo subia e descia na mesma velocidade. Fala
fraca — a um metro o nível é 0,003 a 0,004, contra 0,01 a 0,055 de perto —
empurrava o fundo para cima antes de passar do limiar, o limiar subia junto, e a
frase nunca começava. Agora o fundo sobe devagar e desce depressa, e o piso do
limiar caiu de 0,006 para 0,003.

### O que não está provado

- **Voz de gente.** Tudo acima é voz sintética, limpa e sem sotaque.
- **Com o motor ligado.** O motor faz barulho, e o recorte nunca ouviu isso.
- **O texto não volta para o robô.** Aparece na tela do PC e para ali. Devolver
  não é só mandar a frase pela serial: o console do cérebro trata cada caractere
  como tecla, e "was" seria frente, esquerda e ré.
- Na ESP32-CAM o despejo sai pelo UART0 a 115200: cinco segundos de fala levam
  uns dezoito para atravessar. Compila, e não foi medido.

---

## 2026-10-01 (3) — A foto da XIAO, e o README que cabe numa tela

Henrique pediu três coisas: a pinagem bem clara no GitHub, a página compacta e
coerente para quem chega, e uma foto tirada pela câmera da XIAO.

### O README

Reescrito de 238 linhas para 181, com a **pinagem como peça central**:
um diagrama das duas placas e quatro tabelas — cérebro ↔ corpo, corpo → pontes
e servos, cérebro → amplificador, alimentação. O que saiu de lá não se perdeu:
o porquê de cada decisão já estava nos `docs/` e neste diário, e o README
repetia. Agora ele aponta.

### A foto, e quatro tentativas de clareá-la que não funcionaram

A sala estava escura, e a foto saiu escura: **3 274 bytes** em 320×240, contra
os 4 271 da tarde. O sensor desta Sense é um **OV3660** (`0x3660`), não o OV2640
para o qual os ajustes de pouca luz foram medidos — o `?` do console passou a
dizer qual é.

| Tentativa | Resultado |
| --- | --- |
| Alvo da exposição automática no máximo (`set_ae_level(2)`) | 3 141 bytes — nada |
| Exposição longa pelo modo noturno do OV3660 (0x3A00 e os tetos em 0x3A02/14) | 3 160 bytes — nada |
| Esticar os níveis no PC | clareia, e vira um mosaico: o JPEG não guardou o que não havia |
| A tela do notebook, toda branca, como lâmpada | 3 366 bytes — a câmera não olhava para lá |

Nenhuma das três mudanças de firmware ficou. Ajuste que não muda a medida não é
ajuste, é palpite com cara de código — e os registradores do OV3660 eu escrevi
de memória, sem datasheet na mão. O que ficou foi só a identificação do sensor.

A foto escura entrou em `evidencias/marcos/` do jeito que saiu, com a legenda
dizendo que a sala estava escura. Com luz, é `p` no console e trocar o arquivo.

---

## 2026-10-05 — A DevKit vira o corpo: controle de PS4, e uma página para dirigir

Henrique decidiu seguir com uma **ESP32 DevKit** comum no lugar do C3, e
acrescentar duas formas de dirigir: o **controle de PS4** e uma **página web**
servida pelo robô, o mais minimalista possível. O resto fica igual: o cérebro é
a XIAO Sense, com a voz e a câmera dela.

### Por que a DevKit resolve o controle

O DualShock 4 fala **Bluetooth clássico**. O C3 e a S3 só fazem BLE; o ESP32
clássico é o único da família que ainda tem o clássico. Então o controle mora no
corpo, e isso tem uma vantagem que não foi planejada: ele continua valendo com o
cérebro travado, desligado ou regravando a si próprio.

**A biblioteca.** O Bluepad32 (que pareia sem truque nenhum) só existe dentro de
um core Arduino próprio, mais antigo, ou num projeto ESP-IDF — trocar qualquer
um dos dois mudaria a estrutura inteira do repositório. A
[PS4Controller](https://github.com/pablomarquez76/PS4_Controller_Host), de Juan
Pablo Marquez, compilou no core 3.x que o projeto já usa, e entrou fixa num
commit.

**O preço dela é o pareamento:** o controle só se conecta ao endereço que
guarda. `scripts/pareia_ps4.py` lê esse endereço pelo USB do controle e grava o
do robô no lugar — o que o SixaxisPairTool faz, sem instalar programa nenhum.

### A pinagem da DevKit

As duas pontes em **cinco pinos seguidos** de um lado (32, 33, 25, 26, 27);
servos (19, 18) e enlace (RX2/TX2, 16 e 17) lado a lado do outro. Nenhum pino
de *strapping*, e o console fica no UART0, que é o USB da placa — log e enlace
nunca dividem fio, a armadilha que o C3 tinha.

### Quem tem a vez

Três caminhos movem o robô agora. As regras ficaram em `docs/03`:

- o **controle ganha** enquanto é usado, e até 500 ms depois — o `M` do cérebro
  recebe `OK` e não move nada;
- **`STOP` vale sempre**;
- o controle **alimenta o mesmo failsafe**, e desconectar no meio para na hora;
- o **celular** só move com o dedo na seta: sem repetição por 400 ms, o cérebro
  para o robô — porque o failsafe do corpo vê o cérebro vivo, e não o telefone
  morto.

### A página e a rede própria

Sem `secrets.h`, o cérebro **cria a própria rede** (`feijao-com-farinha`) e a
página fica em `192.168.4.1`. A senha sai do endereço da placa — o repositório é
público, e uma senha padrão escrita aqui seria a de todo robô igual a este. A
página tem 2,2 KB: a foto em cima, cinco teclas embaixo, nada mais.

### Medido

| | Previsto | Medido |
| --- | --- | --- |
| Autoteste na DevKit | 62 passam | **62 de 62, em 213 ms** |
| Bluetooth no primeiro boot | sobe | **não subiu**: `ESP_ERR_INVALID_STATE` |
| Bluetooth com `btInUse()` | sobe | **sobe**, endereço `3c:8a:1f:77:01:76` |
| Rede própria da XIAO | visível | **visível, -79 dBm** a meio metro do PC |
| Brilho médio da foto, antes | — | **24 de 255** |
| Brilho médio da foto, depois | — | **118 de 255**, mesma cena |

**O Bluetooth que não subia.** O core 3.x libera a memória do Bluetooth antes do
`setup()`, a menos que o programa declare `btInUse()`. A biblioteca não declara,
e o erro (`initialize controller failed`) não fala em memória. Uma função de
três linhas em `main_corpo.cpp`.

**As fotos escuras da XIAO, uma semana depois.** No dia 01 eu concluí que "com
pouca luz esta câmera não vai além", depois de quatro tentativas de clarear. Era
falso. O sensor da XIAO é um **OV3660**, e o `set_gainceiling(GAINCEILING_128X)`
escrito para o OV2640 grava, no OV3660, o número cru do enum num registrador que
conta em dezesseis avos: **o "128×" virava um teto de 0,375×**. O ajuste para
pouca luz escurecia a câmera. Achado lendo o driver, quando três fotos seguidas
deram o mesmo brilho — exposição estável e escura demais para ser só a sala.

**`-79 dBm` ao lado do PC** diz que a antena externa da XIAO não está encaixada.
Sem ela, a rede do robô alcança poucos metros.

### A máquina, de novo

O Windows passou a bloquear o `esptool.exe` do PlatformIO (`Error 4551`, Device
Guard), e toda compilação parou no `bootloader.bin`. O mesmo esptool roda pelo
Python do penv: `scripts/esptool_windows.py` troca o `.exe` por um `.cmd` de uma
linha, só no Windows.

### O que não está provado

- **Motor girando** e as placas conversando: os três fios não estão ligados.
- **O controle conectado**: não havia controle no USB para parear.
- **Dirigir pelo celular**: entrar na rede do robô derrubaria a internet do PC,
  então a página foi conferida num navegador local, não num telefone.

**Fechamento, no CI.** No Linux do GitHub os sete ambientes compilam, com a
biblioteca do PS4 e o `esptool_windows.py` sem efeito, como deve. O clang-format
que o Windows não deixou rodar aqui reclamou de espaçamento em quatro arquivos;
o diff veio do próprio job e entrou num commit à parte, sem mudar comportamento
— as placas não foram regravadas por isso. Com ele, os dois jobs passaram.

## 2026-10-05 (2) — O controle de PS4 conecta: faltava a chave

Controle e DevKit no USB do PC. O controle já guardava o endereço do corpo
(`3c:8a:1f:77:01:76`); sem o cabo, apertando PS, **piscava sem parar e não
conectava**. O log do corpo não mostrava nada — a biblioteca só fala depois que
o canal abre.

### Achar o passo que falha

O corpo passou a registrar os eventos do rádio (`[ps4] radio: ...`), e o log da
biblioteca foi ligado numa gravação à parte (`FEIJAO_PS4_DEPURA`).

| Tentativa | O que o rádio mostrou | Conclusão |
| --- | --- | --- |
| Como estava | chegou, **saiu em 0,35 s, motivo 0x113** (o controle encerrou); nenhum canal L2CAP aberto | o controle desiste antes do HID |
| Apagar pareamentos velhos da placa (a dica do repositório original) | **zero** guardados | não era isso |
| Cadastrar no corpo a chave do script | **motivo 0x105: falha de autenticação** | o controle confere uma chave, e a dele era outra |
| A mesma chave, na ordem inversa | 0x105 de novo | a chave dele **não era a do script**: o endereço tinha sido gravado antes, com outra |
| Gravar a chave do robô no controle, pelo cabo | **conectou**, barra verde | |

**Por que piscava.** O DualShock 4 só se conecta ao endereço que guarda, e a cada
conexão confere uma **chave de enlace** — os dois combinados pelo cabo, quando o
PS4 pareia. O script gravava a chave no controle, mas o corpo não a conhecia:
respondia "não tenho", e o controle desistia. O README da biblioteca só fala
do endereço.

**O conserto**, nos dois lados:

- o corpo guarda na NVS o endereço do controle e, no boot, **antes de atender**,
  cadastra a chave na pilha do Bluetooth (`BTA_DmAddDevice`, fora da API
  pública — a biblioteca já usa o L2CAP do mesmo jeito). A pilha guarda a chave
  de trás para a frente; assim conectou;
- `scripts/pareia_ps4.py --corpo COM4` grava no controle o endereço do corpo e a
  chave, e conta ao corpo qual controle aceitar (`PAREIA aa:bb:...`, só pelo USB);
  o corpo reinicia já com a chave.

**O que caiu junto:** a opção de o corpo se passar pelo console (`PS4_MAC`). Ela
não tem como funcionar — falta a chave do console.

### Medido

| | Medido |
| --- | --- |
| Da chegada pelo rádio ao "controle conectado" | **0,6 s** |
| Manche para a esquerda / direita | `M -100 100` / `M 100 -100` |
| Manche de volta ao centro | `M 0 0` na hora |
| ✕ | `X: freio` |
| Pareamento depois de regravar o firmware | **continua** (fica na NVS) |

O log do manche (`[ps4] M e d`) é o que prova, sem motor ligado, que a mão chega
às rodas; sai no máximo a cada 150 ms, parada e freio na hora.

`ASSERT_WARN(21 16), in lc_task.c` aparece a cada conexão. Vem do controlador
Bluetooth fechado da Espressif, e não atrapalhou nenhuma.

### O que não está provado

- **Frente e trás no manche** só pelo autoteste: o log da bancada mostrou os
  lados, o centro e o freio.
- **Motor girando**: não visto nesta sessão.
- Um segundo controle: o corpo aceita **um** de cada vez — parear outro troca.

## 2026-10-05 (3) — As rodas giram; ✕ tira foto, gatilhos movem os servos

### O teste do Henrique

Com o pareamento novo gravado, Henrique acionou as duas placas, com motores e
pontes ligados: **as rodas giraram, e o controle respondeu bem — conexão e
reação boas**. É a primeira vez que um motor deste robô gira. A DevKit não estava
no USB do PC, então não há log deste teste: o registro é o que ele viu.

### O controle ganha botões

| Controle | Faz | Por onde |
| --- | --- | --- |
| manche esquerdo | anda e vira | corpo, direto |
| **○** | freia, enquanto apertado | corpo, direto — era o ✕ |
| **✕** | foto | o corpo manda `>p` ao cérebro pelo enlace |
| **L2** / **R2** | servo 1 / servo 2, proporcional | corpo, direto |

**O ✕ mudou de função** porque Henrique o quis para a foto. O freio foi para a
○; soltar o manche continua parando o robô na hora.

**A foto.** A câmera é do cérebro e o controle fala com o corpo, então o corpo
**pede** a foto pelo caminho de console que o enlace já tinha (`>p`, o mesmo de
quem digita `>p` no USB do corpo) — nada muda no protocolo de movimento. O
cérebro da XIAO não atendia esse caminho (só o `teste_som` atendia); agora as
teclas que chegam do corpo entram numa fila e o `loop()` as executa, porque uma
foto leva 1,5 s e a task do enlace não pode segurar o heartbeat. A foto:

- fica guardada — é a que a página do celular mostra;
- sai em base64 no USB da XIAO, e `scripts/fotos.py` a salva em
  `evidencias/fotos/` (fora do git);
- vira um relato curto para o corpo: `[cerebro] foto 320x240, 10226 bytes`.

Uma foto por aperto, e no máximo uma a cada 2 s — pedido no meio da anterior é
ignorado, não enfileirado.

**Os servos.** Gatilho solto, servo em 90°; apertado até o fim, 180°
(`PS4_SERVO_CURSO`, negativo inverte). O corpo só mexe no servo quando o
gatilho **muda**, então um `S` do cérebro não é desfeito por um gatilho parado.
Se o controle cai com um gatilho apertado, o servo volta ao repouso.

O relato do cérebro no USB do corpo passou de `[cam]` para `[cerebro]` — o
nome vinha da ESP32-CAM.

### Medido

| | Medido |
| --- | --- |
| Foto pedida pelo PC (`fotos.py --agora`) | **salva**, 320×240, 10 226 bytes, imagem inteira |
| Foto pedida pelo corpo (`>p` no USB dele) | **não chegou**: o cérebro diz `corpo MUDO, 251 enviados, 0 respostas` |
| Autoteste na DevKit, com os gatilhos | **65 de 65**, em 224 ms |
| Os sete ambientes | compilam |

**Os três fios do enlace não estão ligados.** O controle, os motores e o
pareamento não precisam deles — por isso o teste de hoje andou. A foto do ✕
precisa: XIAO D6 → DevKit GPIO16, XIAO D7 → GPIO17, GND → GND.

### O que não está provado

- **A foto pelo ✕**, de ponta a ponta: falta o enlace.
- **Os servos pelos gatilhos**: a conta passa no autoteste; o servo de verdade
  ainda não foi visto.

## 2026-10-05 (4) — A câmera vira os olhos do Jaspy

Henrique disse para que servem as fotos: **a câmera é a visão do Jaspy**. A
página do celular é só a olhada rápida.

**Uma foto virou oito.** A câmera guardava uma foto, sobrescrita a cada pedido.
Agora guarda as **últimas 8** na PSRAM, cada uma com um número que cresce no
boot; a nona apaga a mais velha. Console, enlace e página rodam todos no
`loop()` do cérebro, então nenhuma foto é lida enquanto outra é gravada — sem
trava.

**O que o Jaspy pede**, na rede do robô:

| | |
| --- | --- |
| `GET /foto.jpg` | a mais recente |
| `GET /foto.jpg?n=12` | uma pelo número, enquanto guardada |
| `GET /fotos` | `{"b":<boot>,"fotos":[12,11,...]}` |
| `POST /foto` | tira uma agora |

**A página** ganhou uma fileira com as últimas fotos, embaixo da grande: tocar
numa a mostra em cima, e a fileira se atualiza sozinha a cada 3 s — a foto do ✕
aparece sem ninguém tocar em nada. A foto pelo número pode ficar no cache do
telefone (ela nunca muda), então cada atualização baixa só a nova; o `b`, que
muda a cada boot, impede que a foto 1 de ontem apareça no lugar da de hoje.

O relato do cérebro para o corpo leva o número (`[cerebro] foto 3: 320x240,
9924 bytes`), e o `?` do console mostra quantas estão guardadas.

### Medido

| | Medido |
| --- | --- |
| 10 fotos seguidas pelo console | **8 guardadas, da 3 à 10** |
| PSRAM com as 8 | 7 736 KB livres de 8 192 (antes: 7 816) |
| A página, num navegador do PC contra respostas falsas | foto, número, fileira e direcional no lugar |
| A foto nova do README | 320×240, 9 924 bytes — o robô na bancada, visto da XIAO |

### O que não está provado

- **A página num telefone**, com a XIAO de verdade: entrar na rede do robô tira
  a internet deste PC.
- **O Jaspy pedindo foto**: o endereço existe; o lado do Jaspy não.
