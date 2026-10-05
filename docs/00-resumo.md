# Resumo — onde o projeto está, e para onde vai

*Atualizado em 05/10/2026. O registro completo, com cada medida, está no
[`diario.md`](../diario.md); aqui fica só o essencial.*

## Em uma frase

Um robô de duas placas — a **XIAO ESP32-S3 Sense** pensa, a **ESP32 DevKit**
anda — que se dirige pelo controle de PS4 ou pelo celular, fotografa e
transcreve fala. O firmware roda nas duas placas, e **as rodas já giram pelo
controle**. **As duas placas ainda não foram ligadas entre si.**

## O caminho até aqui

| Quando | O que se tentou | O que saiu |
| --- | --- | --- |
| 28/09 | Nascer com as duas placas compilando | Protocolo UART em texto, failsafe no corpo (para com 1 s de silêncio), autoteste sem hardware. Custo: o Windows corta caminhos em 260 caracteres e a instalação do core falha sem dizer por quê |
| 29/09 | Placas na mesa | A XIAO S3 Sense estava em outro projeto, então **a ESP32-CAM virou o cérebro**. O autoteste (56 de 56) achou um defeito na rampa antes de existir motor. A câmera e o microfone disputavam o I2S0 — câmera primeiro, microfone fixo no I2S1 |
| 29/09 | Failsafe e primeira foto | Failsafe medido em **1001 ms**. A primeira foto saiu em base64 pela serial; a primeira tentativa veio preta, e o conserto foi ganho de 128× e doze quadros descartados |
| 29/09 | Console por rede, foto pelo celular, reconhecimento de objetos | Os três **escritos e compilando**. O reconhecimento (MobileNetV2 no PC) foi provado contra a foto salva. O console e a página **nunca rodaram na placa**: falta `secrets.h` com o Wi-Fi |
| 30/09 | Tirar uma foto com o robô montado | **Impossível como estava.** O console da CAM só existe no UART0, e o fio entre as placas é outra UART, que só levava comando de motor |
| 30/09 | Teste simples: som → motor (`teste_som`) | Primeiro, enlace mudo nos dois sentidos e microfone só com zeros: era fiação. Refeita, **o enlace CAM → corpo funcionou**. O som disparava em laço — defeito de lógica do teste, corrigido |
| 30/09 | Ler a CAM pelo adaptador USB dela | **Não é o caminho:** o adaptador ocupa o header inteiro, e com ele encaixado não há microfone nem enlace |
| 30/09 | O enlace carrega tudo | Relato (`#`), console (`>`), ponte crua (`PONTE`) e **gravação de firmware pela serial**. A gravação foi provada na CAM: 354 KB em 43 s, MD5 conferido |
| 01/10 | **Trocar a ESP32-CAM pela XIAO S3 Sense** | A XIAO saiu do FarmIO. USB próprio, microfone embutido, 8 MB de PSRAM: os três problemas da CAM somem. `cerebro` e `teste_som` **rodaram nela** — câmera, foto, microfone medido |
| 01/10 | **Fala para texto** | A placa recorta cada frase e manda pelo USB; o PC transcreve com Whisper, sem nuvem. Primeira tentativa: 1 frase de 3 — o ruído de fundo subia junto com a fala fraca. Corrigido: **5 de 5**, quase literais |
| 05/10 | **A DevKit vira o corpo; controle de PS4 e página para dirigir** | O PS4 fala Bluetooth clássico, que só o ESP32 clássico tem. O Bluepad32 exigiria trocar o core; a biblioteca PS4Controller compilou no core 3.x. A XIAO cria a própria rede e serve uma página mínima: foto e direcional |
| 05/10 | **As fotos escuras da XIAO** | O teto de ganho pensado para o OV2640 virava 0,375× no OV3660 da XIAO. Corrigido: brilho de **24 para 118** de 255, na mesma cena |
| 05/10 | **O controle de PS4 de verdade** | Piscava sem conectar: chegava ao corpo e desistia em 0,35 s. Ele confere uma **chave de enlace** a cada conexão, e o corpo não a conhecia. Agora o script grava a chave no controle e conta ao corpo qual controle aceitar: **conectou, e o manche chega às rodas** |
| 05/10 | **O robô ligado, pelo controle** | Henrique acionou as duas placas: **as rodas giraram, com boa resposta e conexão estável**. Depois: ✕ tira foto (o corpo pede ao cérebro pelo enlace), ○ freia, L2/R2 movem os servos. A foto pedida pelo PC já sai da XIAO e vira arquivo; a do ✕ espera os fios do enlace |
| 05/10 | **A câmera vira os olhos do Jaspy** | A XIAO guarda as últimas 8 fotos e as serve na rede (`/foto.jpg`, `/fotos`, `POST /foto`) — para o Jaspy ver pelo robô. A página ganhou uma fileira com elas, para olhar rápido |

## Como está hoje

**Nas placas**

| Placa | Firmware | Porta |
| --- | --- | --- |
| XIAO ESP32-S3 Sense | **`cerebro`** de 05/10 — câmera, microfone, escuta de frases, página, enlace | COM13 |
| ESP32 DevKit | **`corpo`** de 05/10 — motores, servos, failsafe, controle de PS4 | COM4 |
| ESP32-C3, ESP32-CAM | `corpo_c3`, `cerebro_cam` — **reserva**, fora do robô | — |

**Provado**

- Autoteste **65 de 65 na DevKit**, incluindo as contas do manche e dos gatilhos; failsafe
  em 1001 ms.
- **XIAO:** câmera (OV3660) com brilho médio 118 de 255, microfone PDM, 8 MB de
  PSRAM, e **fala para texto** — 5 frases de voz sintética, todas transcritas.
- **Controle de PS4 dirigindo o robô**: conecta à DevKit, e as rodas giram com
  boa resposta — teste do Henrique com as duas placas ligadas.
- **Foto para o PC**: a XIAO tira, guarda para a página e solta no USB;
  `scripts/fotos.py` salva (320×240, 10 KB).
- **As últimas 8 fotos na memória**: 10 tiradas, ficaram da 3 à 10, em 80 KB
  de PSRAM.
- **Rede própria do robô** no ar e visível do PC; a página foi conferida num
  navegador.

**Não provado**

- **As duas placas conversando**: faltam os três fios — e com eles a foto do
  ✕ (hoje o corpo pede e o cérebro não ouve).
- **Servos pelos gatilhos**: a conta passa no autoteste; o servo de verdade
  ainda não foi visto.
- **Dirigir pelo celular**, e a fileira de fotos: a página não foi aberta num
  telefone ainda (só no PC, contra respostas falsas).
- **O texto virar ordem.** Hoje ele aparece na tela do PC e para ali.
- Fala de gente, e com o motor ligado; alto-falante.

## Aonde queremos chegar

O objetivo não mudou: um robô que anda pela sala **sem nunca sair andando
sozinho**, ouve, fala, vê, e é o mesmo personagem do
[Jaspy](https://github.com/popliosemigod/Jaspy) com rodas.

O que falta, na ordem em que destrava o resto:

1. **Ligar os três fios** entre as placas (D6 → GPIO16, D7 → GPIO17, GND) — e
   ver a foto do ✕ chegar.
2. **Dirigir pelo celular**, com a antena da XIAO no lugar.
3. **O Jaspy enxergar pelo robô:** pedir `/foto.jpg` e descrever o que vê — a
   câmera é a visão dele.
4. **O texto virar ordem:** o que foi transcrito voltar para o robô como
   intenção (andar, parar, virar), com a parada valendo antes de tudo.
5. **Falar de volta:** síntese de voz, o MAX98357A, e a palavra *"para"*
   reconhecida sem rede ([05](05-a-voz.md)).
6. **Os ensaios de bancada que restam** ([04](04-roteiro-de-bancada.md)): duty
   mínimo de partida, rampa, limites dos servos, e o robô no chão.

## Operar agora

```powershell
pio run -e cerebro -t upload --upload-port COM13   # grava a XIAO
pio run -e corpo   -t upload --upload-port COM4    # grava a DevKit
python scripts/pareia_ps4.py --corpo COM4          # pareia (controle e DevKit no USB)
python scripts/ouve.py --porta COM13               # fala para texto
python scripts/fotos.py --porta COM13              # salva as fotos do ✕
```

Celular: rede **feijao-com-farinha** (senha no console do cérebro, `?`), página
em **http://192.168.4.1**. Console da XIAO: `w a s d x` movem, `p` foto, `o`/`q`
escuta, `?` estado. Controle: manche anda, ○ freia, ✕ foto, L2/R2 servos.
Console da DevKit: `M 40 40`, `STOP`, `EN 0`/`EN 1`.

No Windows, o `pio` roda pelo **PowerShell**, com `PLATFORMIO_CACHE_DIR` num
caminho curto; o bloqueio do `esptool.exe` o projeto contorna sozinho.
