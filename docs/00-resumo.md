# Resumo — onde o projeto está, e para onde vai

*Atualizado em 01/10/2026. O registro completo, com cada medida, está no
[`diario.md`](../diario.md); aqui fica só o essencial.*

## Em uma frase

Um robô de duas placas — a **XIAO ESP32-S3 Sense** pensa, o **ESP32-C3** anda —
com o firmware inteiro escrito e compilando. Na XIAO a câmera já fotografou e
**o robô já transcreve fala**; no C3 o failsafe já foi medido. **Nenhum motor
foi visto girando, e a XIAO e o C3 ainda não conversaram.**

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

## Como está hoje

**Nas placas**

| Placa | Firmware | Porta |
| --- | --- | --- |
| XIAO ESP32-S3 Sense | **`cerebro`** de 01/10 — câmera, microfone, escuta de frases, enlace | COM13 (USB próprio) |
| ESP32-C3 | `corpo`, com relato, console e `PONTE` | COM7 (USB próprio) |
| ESP32-CAM | `teste_som_cam` de 30/09 — **reserva**, fora do robô | COM6, só com o adaptador |

**Provado**

- Autoteste 56 de 56; failsafe em 1001 ms; recusa de comando fora de faixa.
- **XIAO, firmware `cerebro`:** câmera ok, foto real de 320×240, 8 MB de PSRAM
  vistos, microfone PDM ok.
- **XIAO, `teste_som`:** microfone embutido com nível ambiente de 0,001 a 0,002.
- **Fala para texto:** 5 frases de voz sintética a ~1 m, todas recortadas e
  transcritas, em 2,5 a 3,6 s cada (uma levou 8,9 s).
- Com a ESP32-CAM: enlace cérebro → corpo, `PONTE` no C3, e gravação de firmware
  pela serial.

**Não provado**

- **Motor girando.** O corpo recebe o comando; ninguém confirmou a roda.
- **O enlace entre a XIAO e o C3.** Na XIAO sozinha o corpo aparece `MUDO`, como
  deve; falta ligar os três fios e ver o `PONG`.
- **Fala de gente**, e com o motor ligado: a transcrição só foi medida com a voz
  sintética do PC, numa sala em silêncio.
- **O texto virar ordem.** Hoje ele aparece na tela do PC e para ali.
- O limiar de 0,02 do `teste_som` é palpite até alguém bater palma.
- Servos, alto-falante, Wi-Fi, console por rede, foto pelo celular.

## Aonde queremos chegar

O objetivo não mudou: um robô que anda pela sala **sem nunca sair andando
sozinho**, ouve, fala, vê, e é o mesmo personagem do
[Jaspy](https://github.com/popliosemigod/Jaspy) com rodas.

O que falta, na ordem em que destrava o resto:

1. **Ligar a XIAO ao C3** (D6 → GPIO20, D7 → GPIO21, GND) e ver o enlace nos
   dois sentidos.
2. **Ver o motor girar**, pelo console (`w`, `x`) ou pelo `teste_som`.
3. **O texto virar ordem:** o que foi transcrito voltar para o robô como
   intenção (andar, parar, virar), com a parada valendo antes de tudo.
4. **Wi-Fi** (`secrets.h`): console por rede, foto pelo celular, reconhecimento
   de objetos com a foto ao vivo.
5. **Falar de volta:** síntese de voz, o MAX98357A, e a palavra *"para"*
   reconhecida sem rede — que só a XIAO faz ([05](05-a-voz.md)).
6. **Os ensaios de bancada que restam** ([04](04-roteiro-de-bancada.md)): duty
   mínimo de partida, rampa, limites dos servos, e o robô no chão.

## Operar agora

```powershell
pio run -e cerebro -t upload --upload-port COM13     # grava a XIAO pelo USB dela
python scripts/ouve.py --porta COM13                 # fala para texto: o que for dito aparece na tela
pio device monitor -p COM13                          # console da XIAO, teclas sem Enter:
#   w a s d x movem | p foto | o / q liga e desliga a escuta | ? estado

pio device monitor -p COM7                           # console do corpo
M 40 40                                              # motores a 40 % - o failsafe para em 1 s
EN 0     /  EN 1                                     # solta / habilita as pontes H
```

O console e o relato pelo USB do corpo (`>g`, `[cam] ...`) existem só no
`teste_som`; o `cerebro` ainda não os tem.

No Windows, o `pio` precisa rodar pelo **PowerShell** (o instalador dos
toolchains recusa o Git Bash) e com `PLATFORMIO_CACHE_DIR` num caminho curto.
