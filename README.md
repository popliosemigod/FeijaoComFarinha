# Feijão com Farinha

Robô móvel com voz, escuta, câmera e movimento. Duas placas: uma que pensa e
uma que anda.

> **Estado (30/09/2026):** as duas placas ligadas entre si e conversando num
> sentido — o corpo recebe os comandos da ESP32-CAM. O autoteste passou
> (**56 de 56**), o failsafe foi medido, a câmera capturou um quadro real, e a
> CAM já recebe firmware novo pela serial, sem o modo de gravação.
> **Nenhum motor foi visto girando, e o microfone ainda não tem nível medido.**
>
> O resumo do caminho até aqui e do que falta está em
> [`docs/00-resumo.md`](docs/00-resumo.md).

## Objetivo

Um robô que se move pela sala, ouve quem fala com ele e responde — e que é o
mesmo agente que já mora no laboratório [Jaspy](https://github.com/popliosemigod/Jaspy),
só que com rodas.

1. **Mover com segurança**, num robô que nunca sai andando sozinho quando algo
   trava.
2. **Ouvir e falar**, com o reconhecimento rodando fora da placa.
3. **Ver**, com a câmera da própria placa.
4. **Ser o mesmo personagem** que o avatar em AR — um agente, vários corpos.

## Arquitetura

```
   XIAO ESP32-S3 Sense                ESP32-C3 SuperMini
   o CEREBRO                          o CORPO
   camera, mic PDM, MAX98357A         2x BTS7960 (motores)
   Wi-Fi, PSRAM 8 MB, decisao  ──UART──►  2 servos
   heartbeat a cada 250 ms     ◄──────    failsafe: para com 1 s de silencio
                                OK/ERR/PONG
```

**O corpo não usa Wi-Fi, e isso é a metade que protege.** O pior caso de
qualquer defeito no cérebro — travar numa rede ruim, ficar sem memória
decodificando um quadro — é o robô **parar**, nunca sair andando sem comando.

Quem para o robô é o corpo, contando o tempo desde o último comando de
movimento. Nenhum dos dois lados precisa que o outro esteja correto para se
comportar bem.

## O que está gravado nas placas, hoje

| Placa | Firmware | |
| --- | --- | --- |
| ESP32-C3 | `corpo` desta versão | com relato, console e `PONTE` |
| ESP32-CAM | **`teste_som`** de 30/09/2026 | microfone e enlace — **sem câmera** |

A CAM **não** está com o `cerebro_cam`. Está com o teste simples: um som alto
faz o robô andar 1,5 s e parar. Ele liga com o som **armado** — deixe as rodas
livres na primeira ligada.

**O adaptador USB da CAM ocupa o header inteiro.** Com ele encaixado não há
microfone nem enlace, então ler a CAM pelo USB dela e testar o robô montado não
acontecem ao mesmo tempo. Por isso tudo passa pelo **USB do corpo**: o que a CAM
relata, as teclas do console dela e o firmware novo
([`docs/03`](docs/03-protocolo-uart.md#o-que-passa-pelo-enlace-e-não-é-movimento)).

```powershell
pio run -e teste_som
python scripts/grava_pelo_enlace.py      # grava a CAM pelo COM7, sem adaptador
```

Isso vale para o `teste_som`. O `cerebro_cam` ainda não tem o receptor: gravá-lo
agora devolve a câmera e traz de volta a dependência do adaptador.

O passo a passo da montagem, com um teste ao fim de cada etapa, está em
[`docs/06-montagem-do-corpo.md`](docs/06-montagem-do-corpo.md).

Dois resistores de 10 kΩ não são opcionais: um no **EN das pontes** e outro no
**GPIO12 da ESP32-CAM**. O segundo decide se a placa liga ou não.

## Compilar e gravar

```powershell
pio run                          # as duas placas e o autoteste

pio run -e corpo   -t upload     # a C3       (sempre com -e)
pio run -e cerebro -t upload     # a XIAO S3
pio device monitor -b 115200     # console
```

| Ambiente | Placa | |
| --- | --- | --- |
| `corpo` | ESP32-C3 SuperMini | motores, servos, UART, failsafe |
| **`cerebro_cam`** | **ESP32-CAM (AI-Thinker)** | **o cérebro que roda hoje** |
| `cerebro` | XIAO ESP32-S3 Sense | o mesmo cérebro, na placa melhor |
| `teste_som` | ESP32-CAM (AI-Thinker) | o teste simples: som → motor, sem câmera |
| `bancada` | ESP32-C3 SuperMini | o corpo com log detalhado |
| `autoteste` | qualquer ESP32 | a lógica das duas, sem hardware |

### Por que há dois cérebros

Há **uma única XIAO ESP32-S3 Sense** no laboratório, e três projetos a querem.
A ESP32-CAM é o que destrava este aqui enquanto isso.

É a mesma `main_cerebro.cpp` nas duas: muda só a pinagem, escolhida pelo flag
`-DCEREBRO_CAM`. Voltar para a Sense é tirar o flag.

O que a ESP32-CAM custa está em
[`include/config_cerebro_cam.h`](include/config_cerebro_cam.h), na seção *"O QUE
ESTA PLACA NÃO FAZ"*. A perda que muda o projeto: **ESP32 clássico não roda
ESP-SR**, então não existe a palavra *"para"* reconhecida sem rede.

E ela só coube porque este projeto **não usa cartão SD**: os seis GPIOs do SDMMC
são exatamente os que sobraram para a UART e para o microfone.

### Antes da primeira compilação, no Windows

O projeto usa o **Arduino core 3.x** (a API de PWM é `ledcAttach(pino, freq,
bits)`, que só existe do 3.0 em diante). A plataforma oficial do PlatformIO
parou no core 2.0.17, então o core 3.x vem do fork **pioarduino**, já declarado
no `platformio.ini`.

O pacote dele tem arquivos com caminho muito longo, e o Windows corta em 260
caracteres. Sem tratar isso, a instalação falha com uma mensagem que **não** diz
o que aconteceu: *"Failed to install Python dependencies"*, e embaixo um
`FileNotFoundError` num header de nome enorme.

Duas saídas, qualquer uma resolve:

```powershell
# 1. encurtar o diretorio do PlatformIO (nao precisa de administrador)
$env:PLATFORMIO_CORE_DIR = "C:\pio"
pio run

# 2. ou ligar caminhos longos no Windows, uma vez, COMO ADMINISTRADOR
Set-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" `
                 -Name LongPathsEnabled -Value 1
```

Se o que falha é a pasta temporária (`.platformio\.cache\tmp\...` no erro),
encurtar só o cache basta: `$env:PLATFORMIO_CACHE_DIR = "C:\Users\<voce>\.pc"`.

E rode o `pio` pelo **PowerShell**, não pelo Git Bash: o instalador dos
toolchains recusa o MSYS (`MSys/Mingw is not supported`), e o erro que aparece
depois é só `g++ não é reconhecido como um comando`.

No Linux e no CI o problema não existe.

## A primeira foto desta câmera

![Primeira foto da ESP32-CAM — 320x240, 29/09/2026](evidencias/marcos/primeira-foto-20260929.jpg)

Saiu **em base64 pela serial de gravação**, porque esta placa não tem cartão SD —
e não tem porque os seis pinos do cartão são exatamente os que a UART e o
microfone ocupam. O caminho todo está em
[`docs/06-montagem-do-corpo.md`](docs/06-montagem-do-corpo.md#a-primeira-foto-desta-câmera).

## Documentação

| | |
| --- | --- |
| **[00](docs/00-resumo.md)** | **resumo: o caminho até aqui, como está, e o que falta** |
| **[06](docs/06-montagem-do-corpo.md)** | **montagem passo a passo — comece por aqui na bancada** |
| [01](docs/01-hardware-e-pinagem.md) | as duas placas, pinagem e por que são duas |
| [02](docs/02-ligacoes-e-alimentacao.md) | **alimentação, GND, capacitores e o BTS7960 em 3,3 V** |
| [03](docs/03-protocolo-uart.md) | o protocolo, o failsafe, **o console e a gravação pelo enlace**, e quatro propostas em aberto |
| [04](docs/04-roteiro-de-bancada.md) | **roteiro de ensaios**, com as previsões escritas antes |
| [05](docs/05-a-voz.md) | a escolha do serviço de voz — ainda não tomada |
| [07](docs/07-reconhecimento-de-objetos.md) | reconhecimento de objetos e **foto pelo celular** — a câmera diz o que vê, com ImageNet pronto |

## Controle para testes — com ou sem cabo

Pela serial (sempre disponível): abra o monitor a 115200 baud e digite uma
tecla — `w a s d x` movem, `1`/`2` os servos, `t` a rotina completa, `?` o
estado. Lista inteira em
[`docs/06-montagem-do-corpo.md`](docs/06-montagem-do-corpo.md#cartão-de-bolso--o-console-do-cérebro).

**Pelo USB do corpo, com o robô montado:** no monitor do C3, `>` na frente da
tecla manda para o console do cérebro (`>g`, `>x`, `>?`), e o que o cérebro
relata aparece como `[cam] ...`. É o único console que existe sem o adaptador
da CAM — hoje, só no `teste_som`.

Pela rede, sem cabo nenhum: preencha `WIFI_SSID`/`WIFI_SENHA` em
`include/secrets.h` (copiado de `secrets.example.h`) e o robô sobe com um
console de teste em `telnet <ip> 23` — as mesmas teclas, mesmo comportamento.
Detalhe em
[`docs/06-montagem-do-corpo.md`](docs/06-montagem-do-corpo.md#o-mesmo-console-sem-cabo).

Isto é para **teste manual**, não para operação normal: não há autenticação, e
é a mesma decisão que a ponte do Jaspy já toma na LAN de casa.

**Pelo celular, com foto:** `http://<ip-do-robô>/` no navegador — um botão
"Tirar foto" e, depois da primeira, um botão "Baixar" que salva no aparelho
(a galeria de fábrica costuma achar a pasta Download sozinha). Detalhe em
[`docs/07-reconhecimento-de-objetos.md`](docs/07-reconhecimento-de-objetos.md#a-foto-no-celular).

## Em aberto

1. **Qual serviço de voz** ([05-a-voz.md](docs/05-a-voz.md)). A recomendação é
   a ponte do Jaspy na LAN, com ESP-SR local só para a palavra *"para"*.
2. **Ligar a rampa de aceleração?** Está escrita e desligada
   ([03](docs/03-protocolo-uart.md#1-rampa-de-aceleração--recomendo-ligar)).
3. **Acrescentar `STATUS` ao protocolo?** Barato e útil.
4. Checksum e ID de sequência: **não** recomendados agora, e a razão está
   escrita.

## Medido na bancada

| | Previsto | Medido |
| --- | --- | --- |
| Autoteste no ESP32-C3 (ensaio 0) | 50 passam | **56 de 56, em 4 ms** |
| Failsafe dispara (ensaio 1) | 1000–1010 ms | **1001 ms** |
| `M 200 0` | recusa sem mover | **`ERR fora-de-faixa`** |
| `S 1 45` alimenta o relógio? | **não** | **não** — failsafe 1001 ms depois |
| Primeira foto da OV2640 | — | **11 860 bytes**, 320×240 |
| PSRAM livre na ESP32-CAM | 4063 KB de 4096 KB |
| Heap livre, com câmera e I2S de pé | 240 KB |
| Heartbeat com o corpo ausente | 46 enviados, 0 respostas — e o log acusa |
| Enlace CAM → corpo, com os fios refeitos | `M` a cada 250 ms | **chega**: o corpo não cai mais em failsafe |
| Gravação da CAM pela serial (354 KB) | — | **43 s**, MD5 conferido, reiniciou na imagem nova |
| Som → motor, primeira versão | dispara com palma | **dispara em laço**, a cada 3,0 s — lógica corrigida |

## O que não está provado

- **Nenhum motor foi visto girando e nenhum servo se mexeu.** O corpo recebe o
  comando de andar; ninguém confirmou a roda.
- **O microfone não tem nível medido.** Ele dispara o teste, então entrega algum
  sinal — mas o comportamento muda com as pontes H habilitadas, e não se sabe se
  é som ou ruído elétrico.
- **O sentido corpo → CAM do enlace**, e com ele o console e a gravação de
  firmware passando pelo C3. A gravação só foi provada direto no UART0 da CAM.
- Os limites de ângulo dos servos são **de projeto**, não medidos com o
  mecanismo montado.
- O duty mínimo de partida dos motores não existe até o [ensaio
  4](docs/04-roteiro-de-bancada.md#ensaio-4--o-robô-no-chão-pela-primeira-vez).

## Crédito

Projeto do laboratório [Jaspy](https://github.com/popliosemigod/Jaspy). O
desenho do corpo — limites como único caminho até o hardware, parada que para
onde está, e o firmware que precisa compilar sem `secrets.h` — vem do
`firmware/jaspy-corpo` daquele repositório.
