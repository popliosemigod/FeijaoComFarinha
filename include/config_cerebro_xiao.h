// =====================================================================
//  config_cerebro_xiao.h - XIAO ESP32-S3 Sense: pinagem e parametros
//
//  Nao incluir direto: quem inclui e `config_cerebro.h`, que escolhe
//  entre esta placa e a ESP32-CAM.
//
//  O cerebro cuida de camera, microfone, alto-falante, Wi-Fi e decisao.
//  Ele NUNCA aciona motor direto: quem mexe em ponte H e servo e o
//  corpo, do outro lado da UART. A separacao e o que faz o movimento
//  sobreviver a um travamento de rede.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "protocolo.h"

// ---- Identidade -----------------------------------------------------
#define CEREBRO_NOME   "feijao-cerebro-xiao"
#define CEREBRO_VERSAO "0.1.0"

// ---- O que esta placa tem -------------------------------------------
// Lidas por `microfone.h`, `audio.h` e `main_cerebro.cpp`, que assim
// compilam iguais nas duas placas.
#define MIC_PDM          1  // microfone embutido na placa Sense
#define TEM_AMPLIFICADOR 1  // MAX98357A no I2S de saida
#define TEM_ESP_SR       1  // S3: palavra de acordar local e possivel

// ---- UART para o corpo ----------------------------------------------
//
//  Mesma observacao do outro lado: D6/D7 sao GPIO43/44, os pinos do
//  UART0. Isto so e seguro porque o console sai pelo USB CDC.
#define PIN_UART_TX 43  // D6 -> RX do corpo (GPIO16, RX2 da DevKit)
#define PIN_UART_RX 44  // D7 <- TX do corpo (GPIO17, TX2 da DevKit)

// ---- MAX98357A (I2S de saida) ---------------------------------------
// SD solto: o amplificador liga sozinho. Alimentacao em 5V - os picos
// passam de 1 A e o regulador de 3,3 V da placa nao aguenta.
#define PIN_AUDIO_BCLK 1  // D0
#define PIN_AUDIO_LRC  2  // D1
#define PIN_AUDIO_DIN  3  // D2

#define AUDIO_TAXA_HZ 16000  // PCM 16 kHz mono: leve e suficiente para voz

// ---- Microfone PDM embutido na placa Sense --------------------------
// Fica no periferico I2S separado do amplificador, entao pode ter taxa
// propria - gravar em 16 kHz enquanto se toca em 16 kHz nao disputa.
#define PIN_MIC_CLK  42
#define PIN_MIC_DATA 41

#define MIC_TAXA_HZ 16000

// ---- Camera da placa Sense ------------------------------------------
// Pinos internos do conector da placa - nao sao escolha, sao o que a
// Seeed roteou. Estao aqui para que o firmware nao dependa de um
// cabecalho de terceiro que pode mudar de nome.
#define PIN_CAM_PWDN  -1
#define PIN_CAM_RESET -1
#define PIN_CAM_XCLK  10
#define PIN_CAM_SIOD  40
#define PIN_CAM_SIOC  39
#define PIN_CAM_Y9    48
#define PIN_CAM_Y8    11
#define PIN_CAM_Y7    12
#define PIN_CAM_Y6    14
#define PIN_CAM_Y5    16
#define PIN_CAM_Y4    18
#define PIN_CAM_Y3    17
#define PIN_CAM_Y2    15
#define PIN_CAM_VSYNC 38
#define PIN_CAM_HREF  47
#define PIN_CAM_PCLK  13

// ---- Livres ---------------------------------------------------------
// D3 (GPIO4), D4 (GPIO5), D5 (GPIO6). Mais D8/D9/D10 se o cartao SD da
// placa Sense nao for usado.

// ---- Prazos do enlace -----------------------------------------------
// Sem PONG por este tempo, o cerebro considera o corpo ausente e para
// de mandar movimento. Nao e ele quem freia o robo - disso cuida o
// failsafe do corpo -, mas insistir em falar com quem nao responde
// esconde o defeito do log.
#define CORPO_MUDO_MS 1500

// De quanto em quanto tempo o cerebro pergunta se o corpo esta vivo,
// quando nao ha movimento em curso. Com movimento, o proprio `M`
// periodico ja serve de pergunta.
#define PING_OCIOSO_MS 500

// ---- Wi-Fi ----------------------------------------------------------
//
//  Credencial vem do secrets.h local, que NAO vai para o git. Sem ele
//  isto compila com valores vazios e o cerebro sobe sem rede, dizendo
//  isso no log - e a regra que permite o CI compilar sem segredo
//  nenhum, e a que faz o robo andar mesmo sem internet.
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

// Servico de voz: ainda NAO decidido (ver docs/05-a-voz.md). Enquanto
// nao for, `voz.h` expoe a interface e nenhum endereco real.
#ifndef VOZ_SERVIDOR
#define VOZ_SERVIDOR ""
#endif
