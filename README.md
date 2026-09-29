# Feijão com Farinha

Robô móvel com voz, escuta, câmera e movimento. Duas placas: uma que pensa e
uma que anda.

> **Estado:** firmware das duas placas escrito e compilando.
> **Nenhum motor foi ligado, nenhum servo girou, e o robô ainda não existe
> montado.**

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
| `cerebro` | XIAO ESP32-S3 Sense | câmera, mic, áudio, Wi-Fi, decisão |
| `bancada` | ESP32-C3 SuperMini | o corpo com log detalhado |
| `autoteste` | qualquer ESP32 | a lógica das duas, sem hardware |

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

No Linux e no CI o problema não existe.

## Documentação

| | |
| --- | --- |
| [01](docs/01-hardware-e-pinagem.md) | as duas placas, pinagem e por que são duas |
| [02](docs/02-ligacoes-e-alimentacao.md) | **alimentação, GND, capacitores e o BTS7960 em 3,3 V** |
| [03](docs/03-protocolo-uart.md) | o protocolo, o failsafe e quatro propostas em aberto |
| [04](docs/04-roteiro-de-bancada.md) | **roteiro de ensaios**, com as previsões escritas antes |
| [05](docs/05-a-voz.md) | a escolha do serviço de voz — ainda não tomada |

## Em aberto

1. **Qual serviço de voz** ([05-a-voz.md](docs/05-a-voz.md)). A recomendação é
   a ponte do Jaspy na LAN, com ESP-SR local só para a palavra *"para"*.
2. **Ligar a rampa de aceleração?** Está escrita e desligada
   ([03](docs/03-protocolo-uart.md#1-rampa-de-aceleração--recomendo-ligar)).
3. **Acrescentar `STATUS` ao protocolo?** Barato e útil.
4. Checksum e ID de sequência: **não** recomendados agora, e a razão está
   escrita.

## O que não está provado

- **Nada foi ligado.** O firmware compila; nenhum motor girou, nenhum servo se
  mexeu, nenhum quadro foi capturado.
- Os limites de ângulo dos servos são **de projeto**, não medidos com o
  mecanismo montado.
- O duty mínimo de partida dos motores não existe até o [ensaio
  4](docs/04-roteiro-de-bancada.md#ensaio-4--o-robô-no-chão-pela-primeira-vez).

## Crédito

Projeto do laboratório [Jaspy](https://github.com/popliosemigod/Jaspy). O
desenho do corpo — limites como único caminho até o hardware, parada que para
onde está, e o firmware que precisa compilar sem `secrets.h` — vem do
`firmware/jaspy-corpo` daquele repositório.
