# Resumo — onde o projeto está, e para onde vai

*Atualizado em 30/09/2026. O registro completo, com cada medida, está no
[`diario.md`](../diario.md); aqui fica só o essencial.*

## Em uma frase

Um robô de duas placas — a **ESP32-CAM** pensa, o **ESP32-C3** anda — com o
firmware inteiro escrito e compilando. A câmera já fotografou, o failsafe já foi
medido e as placas já conversam num sentido. **Nenhum motor foi visto girando, e
o microfone ainda não tem um nível medido.**

## O caminho até aqui

| Quando | O que se tentou | O que saiu |
| --- | --- | --- |
| 28/09 | Nascer com as duas placas compilando | Protocolo UART em texto, failsafe no corpo (para com 1 s de silêncio), autoteste sem hardware. Custo: o Windows corta caminhos em 260 caracteres e a instalação do core falha sem dizer por quê |
| 29/09 | Placas na mesa | A XIAO S3 Sense está em outro projeto, então **a ESP32-CAM virou o cérebro**. O autoteste (56 de 56) achou um defeito na rampa antes de existir motor. A câmera e o microfone disputavam o I2S0 — câmera primeiro, microfone fixo no I2S1 |
| 29/09 | Failsafe e primeira foto | Failsafe medido em **1001 ms**. A primeira foto saiu em base64 pela serial; a primeira tentativa veio preta, e o conserto foi ganho de 128× e doze quadros descartados |
| 29/09 | Console por rede, foto pelo celular, reconhecimento de objetos | Os três **escritos e compilando**. O reconhecimento (MobileNetV2 no PC) foi provado contra a foto salva. O console e a página **nunca rodaram na placa**: falta `secrets.h` com o Wi-Fi |
| 30/09 | Tirar uma foto com o robô montado | **Impossível como estava.** O console da CAM só existe no UART0, e o fio entre as placas é outra UART, que só levava comando de motor |
| 30/09 | Teste simples: som → motor (`teste_som`) | Primeiro, enlace mudo nos dois sentidos e microfone só com zeros: era fiação. Refeita, **o enlace CAM → corpo funcionou**. O som disparava em laço — defeito de lógica do teste, corrigido |
| 30/09 | Ler a CAM pelo adaptador USB dela | **Não é o caminho:** o adaptador ocupa o header inteiro, e com ele encaixado não há microfone nem enlace |
| 30/09 | O enlace carrega tudo | Relato (`#`), console (`>`), ponte crua (`PONTE`) e **gravação de firmware pela serial**. A gravação foi provada na CAM: 354 KB em 43 s, MD5 conferido |

## Como está hoje

**Nas placas**

| Placa | Firmware | Porta |
| --- | --- | --- |
| ESP32-C3 | `corpo`, com relato, console e `PONTE` | COM7 (USB próprio) |
| ESP32-CAM | `teste_som` de 30/09 — microfone e enlace, **sem câmera** | COM6, só com o adaptador |

**Provado**

- Autoteste 56 de 56; failsafe em 1001 ms; recusa de comando fora de faixa.
- Câmera: foto real de 320×240 (com o `cerebro_cam`, que não é o que está gravado agora).
- Enlace **CAM → corpo**: o corpo recebe `M`, `STOP` e não cai em failsafe.
- `PONTE` no C3: os bytes da CAM aparecem crus no USB.
- Gravação de firmware pela serial, direto no UART0 da CAM.

**Não provado**

- **Motor girando.** O corpo recebe o comando; ninguém confirmou a roda.
- **Microfone.** Ele dispara o teste, então entrega algum sinal — mas com as
  pontes habilitadas o comportamento muda, e não se sabe se é som ou ruído
  elétrico. Falta ler o número.
- Enlace **corpo → CAM**, e portanto o console e a gravação **passando pelo C3**.
- Servos, Wi-Fi, console por rede, foto pelo celular, voz.

## Aonde queremos chegar

O objetivo não mudou: um robô que anda pela sala **sem nunca sair andando
sozinho**, ouve, fala, vê, e é o mesmo personagem do
[Jaspy](https://github.com/popliosemigod/Jaspy) com rodas.

O que falta, na ordem em que destrava o resto:

1. **Fechar o teste simples.** Religar os fios, ler o microfone pelo COM7,
   ajustar o limiar e ver o motor girar com uma palma.
2. **Provar a gravação pelo C3.** A partir daí o adaptador da CAM não sai mais
   da gaveta.
3. **Levar relato, console e gravação para o `cerebro_cam`.** Hoje só o
   `teste_som` tem; o firmware de verdade, com câmera, ainda depende do
   adaptador.
4. **Wi-Fi** (`secrets.h`): console por rede, foto pelo celular, reconhecimento
   de objetos com a foto ao vivo.
5. **Voz:** escolher o serviço ([05](05-a-voz.md)) e ligar o amplificador.
6. **Os ensaios de bancada que restam** ([04](04-roteiro-de-bancada.md)): duty
   mínimo de partida, rampa, limites dos servos, e o robô no chão.
7. **A XIAO S3 Sense de volta**, quando ficar livre: é tirar um flag, e com ela
   volta a palavra *"para"* reconhecida sem rede.

## Operar agora, só pelo USB do corpo

```powershell
pio device monitor -p COM7          # console do corpo, e o que a CAM relata: [cam] ...

M 40 40                             # motores a 40 % - o failsafe para em 1 s
EN 0     /  EN 1                    # solta / habilita as pontes H
>?                                  # estado do teste na CAM
>d                                  # arma / desarma o som (desarmado so mede)
>+  /  >-                           # sobe / desce o limiar do som
>g  /  >x                           # anda agora / para

pio run -e teste_som                # compila o firmware da CAM...
python scripts/grava_pelo_enlace.py # ...e grava pelo enlace, sem adaptador
```

No Windows, o `pio` precisa rodar pelo **PowerShell** (o instalador dos
toolchains recusa o Git Bash) e com `PLATFORMIO_CACHE_DIR` num caminho curto.
