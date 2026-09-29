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
