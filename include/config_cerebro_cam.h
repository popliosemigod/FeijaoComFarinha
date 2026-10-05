// =====================================================================
//  config_cerebro_cam.h - ESP32-CAM (AI-Thinker) como cerebro
//
//  Nao incluir direto: quem inclui e `config_cerebro.h`.
//
//  ESTA PLACA E O PLANO B, E O PLANO B TEM CUSTO. Ela esta aqui porque
//  ha uma unica XIAO S3 Sense no laboratorio e tres projetos a querem.
//  O que se perde em relacao a Sense esta escrito na secao "O que esta
//  placa NAO faz", no fim - nao em nota de rodape, porque uma delas
//  muda o projeto.
//
//  A ARITMETICA DE PINOS, que e o que torna isto possivel
//  ------------------------------------------------------
//  A ESP32-CAM expoe 10 pinos no header, e a camera ja comeu quase
//  tudo por dentro. Quem sobra:
//
//      GPIO1, GPIO3    UART0 - o console, e como se grava a placa
//      GPIO2, GPIO4, GPIO12, GPIO13, GPIO14, GPIO15
//      GPIO16          NAO: e o CS da PSRAM, e a camera exige PSRAM
//
//  Os seis do meio sao os pinos do CARTAO SD (SDMMC). Com cartao em 4
//  bits sobram dois; sem cartao, sobram seis - e e por isso que ESTE
//  PROJETO NAO USA CARTAO SD. Foi a decisao que fez a placa caber.
//
//  Precisamos de cinco: dois na UART com o corpo, tres no INMP441.
//  Sobra o GPIO4, que e o LED de flash, e que vira o DIN do
//  amplificador quando ele existir - a essa altura o microfone e o
//  amplificador dividem BCLK e WS em full duplex, e o sexto pino
//  fecha a conta exata.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "protocolo.h"

// ---- Identidade -----------------------------------------------------
#define CEREBRO_NOME   "feijao-cerebro-cam"
#define CEREBRO_VERSAO "0.1.0"

// ---- O que esta placa tem -------------------------------------------
#define MIC_PDM          0  // INMP441 externo, I2S padrao (nao PDM)
#define TEM_AMPLIFICADOR 0  // nao ha MAX98357A no estoque ainda
#define TEM_ESP_SR       0  // ESP32 classico: ESP-SR nao roda

// ---- UART para o corpo ----------------------------------------------
//
//  UART2, e nao UART0. O UART0 (GPIO1/GPIO3) e o console e o caminho
//  de gravacao: esta placa nao tem USB nativo, entao perder o UART0
//  seria perder o unico jeito de falar com ela.
//
//  GPIO13 e GPIO14 sao os dois melhores pinos livres: nenhum dos dois
//  e strapping de boot. Foram escolhidos por isso.
#define PIN_UART_TX 14  // -> RX do corpo (GPIO20)
#define PIN_UART_RX 13  // <- TX do corpo (GPIO21)

// ---- INMP441 (I2S de entrada) ---------------------------------------
//
//  O microfone NAO e embutido aqui - sao 5 INMP441 no estoque, e cada
//  um custa tres pinos. Ligacao: VDD em 3,3V, GND no GND, L/R no GND
//  (canal esquerdo, que e o que o firmware le).
#define PIN_MIC_BCLK 15  // SCK do INMP441
#define PIN_MIC_WS   2   // WS do INMP441
#define PIN_MIC_DATA 12  // SD do INMP441   <-- LER A RESSALVA ABAIXO

//  ATENCAO - RESISTOR OBRIGATORIO DE 10 k ENTRE O GPIO12 E O GND.
//
//  O GPIO12 e o MTDI, e o ESP32 le esse pino no boot para decidir a
//  tensao da flash. Alto no boot significa flash em 1,8 V, e a placa
//  NAO INICIA - fica em laco de reset ate o pino baixar. O INMP441 so
//  dirige o SD dentro do proprio slot; antes de o firmware configurar
//  o I2S, a linha flutua.
//
//  O pull-down cobre exatamente essa janela. Nao e refinamento: sem
//  ele a placa liga certo algumas vezes e nao liga outras, o que e o
//  pior defeito possivel de diagnosticar.
//
//  Os outros dois tambem sao strapping, e os dois sao inofensivos
//  aqui: o GPIO15 (MTDO) apenas suprime o log de boot da ROM quando
//  esta alto - o que ate ajuda, porque tira lixo da serial -, e o
//  GPIO2 so importa em modo de gravacao, quando o firmware nem rodou.

#define MIC_TAXA_HZ 16000

//  O MICROFONE VAI NO I2S1, E ISSO NAO E ESCOLHA - E O QUE SOBRA.
//
//  No ESP32 classico a camera nao tem controlador proprio: a interface
//  DVP dela roda sobre o I2S0, em modo paralelo, e nenhum outro serve.
//  Entao o I2S0 e da camera, e o microfone fica com o I2S1.
//
//  Fixar aqui e necessario porque a alocacao automatica NAO percebe o
//  conflito: a camera usa o driver I2S antigo e o microfone usa o
//  novo, e o novo continua achando que o I2S0 esta livre. Ele tenta,
//  falha na alocacao de interrupcao, e o erro nao fala em camera:
//
//      No free interrupt inputs for I2S0 interrupt
//      i2s_init_dma_intr(887): Allocate rx dma channel failed
//
//  O I2S1 nao faz PDM nem camera, so I2S padrao - que e exatamente o
//  que o INMP441 fala.
//  `I2S_NUM_1`, e nao o numero 1: no ESP32 classico `i2s_port_t` e um
//  enum de verdade, e passar inteiro nao compila.
#define MIC_PORTA_I2S I2S_NUM_1

// ---- Amplificador ---------------------------------------------------
// Quando o MAX98357A chegar: ele divide BCLK e WS com o microfone (o
// I2S do ESP32 faz full duplex no mesmo par de clocks) e usa o GPIO4
// como DIN. O GPIO4 e o LED branco de flash - ele vai piscar junto com
// o audio. Solucao, se incomodar: dessoldar o LED.
#define PIN_AUDIO_BCLK PIN_MIC_BCLK
#define PIN_AUDIO_LRC  PIN_MIC_WS
#define PIN_AUDIO_DIN  4
#define AUDIO_TAXA_HZ  16000

// ---- Camera: pinagem do modulo AI-Thinker ---------------------------
// Sao pinos internos da placa, nao ha escolha. Estao aqui, e nao num
// cabecalho de terceiro, para que atualizacao de biblioteca nao mude
// pinagem em silencio.
#define PIN_CAM_PWDN  32
#define PIN_CAM_RESET -1
#define PIN_CAM_XCLK  0
#define PIN_CAM_SIOD  26
#define PIN_CAM_SIOC  27
#define PIN_CAM_Y9    35
#define PIN_CAM_Y8    34
#define PIN_CAM_Y7    39
#define PIN_CAM_Y6    36
#define PIN_CAM_Y5    21
#define PIN_CAM_Y4    19
#define PIN_CAM_Y3    18
#define PIN_CAM_Y2    5
#define PIN_CAM_VSYNC 25
#define PIN_CAM_HREF  23
#define PIN_CAM_PCLK  22

// O XCLK da camera e o GPIO0, que e TAMBEM o pino que precisa ir ao
// GND para gravar. Nao ha conflito em regime, mas explica por que a
// gravacao pede o jumper (ou o botao da placa MB): nao e a camera, e
// o boot.

// ---- Prazos do enlace -----------------------------------------------
#define CORPO_MUDO_MS  1500
#define PING_OCIOSO_MS 500

// ---- Wi-Fi ----------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_SENHA
#define WIFI_SENHA ""
#endif

// A rede propria do robo, quando nao ha rede de casa. Sem AP_SENHA a
// senha sai do endereco da placa - ver main_cerebro.cpp.
#ifndef AP_NOME
#define AP_NOME "feijao-com-farinha"
#endif
#ifndef AP_SENHA
#define AP_SENHA ""
#endif
#ifndef VOZ_SERVIDOR
#define VOZ_SERVIDOR ""
#endif

// =====================================================================
//  O QUE ESTA PLACA NAO FAZ
//
//  1. NAO RODA ESP-SR. O reconhecimento de palavra local so existe no
//     ESP32-S3. Isso derruba a recomendacao de deixar a palavra "para"
//     funcionando sem rede - e essa era a unica ordem que nao deveria
//     depender de Wi-Fi. Mitigacao possivel e MENOS boa: um botao
//     fisico de parada no robo, que e mais confiavel que voz e menos
//     util quando ele ja esta longe da mao.
//
//  2. NAO TEM CARTAO SD neste projeto, por decisao de pinos. Gravar
//     audio ou foto localmente passa a exigir rede.
//
//  3. NAO TEM USB NATIVO. Grava e monitora pela UART0, com adaptador
//     externo (o FTDI ou a placa MB), e o GPIO0 precisa ir ao GND na
//     hora de gravar.
//
//  4. 4 MB de PSRAM em vez de 8, e 520 KB de RAM interna. Suficiente
//     para camera em QVGA e audio em 16 kHz; apertado para os dois em
//     resolucao alta ao mesmo tempo.
//
//  Se a XIAO S3 Sense ficar livre, trocar de volta e mudar UM flag de
//  compilacao: `-DCEREBRO_CAM` sai, e todo o resto do firmware fica
//  igual. O caminho de volta foi mantido aberto de proposito.
// =====================================================================
