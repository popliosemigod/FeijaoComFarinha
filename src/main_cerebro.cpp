// =====================================================================
//  main_cerebro.cpp - XIAO ESP32-S3 Sense: o cerebro
//
//  Ele cuida de camera, microfone, alto-falante e decisao, e manda
//  movimento para o corpo pela UART. Nunca aciona motor direto.
//
//  Tres tarefas, cada uma no seu tempo, e nenhuma esperando a outra:
//
//    corpo-link   (nucleo 0, prio 3)  reenvia movimento a cada 250 ms
//    sentidos     (nucleo 1, prio 2)  le o microfone e alimenta a voz
//    loop()       (nucleo 1, prio 1)  decide, registra, atende console
//
//  A separacao nao e enfeite de arquitetura: e o que garante que uma
//  chamada de rede lenta na decisao nao atrase o heartbeat e nao faca
//  o robo parar no meio da sala.
//
//  O ROBO NAO ANDA SOZINHO AO LIGAR, e isso e decisao de seguranca.
//  A rotina de teste so comeca quando alguem digita `t` no console -
//  a placa costuma ser ligada com o robo na mesa, e uma mesa tem
//  borda. Ate la ele fica parado, mandando `M 0 0`, que e diferente
//  de ficar mudo.
// =====================================================================
#include <Arduino.h>

#include "audio.h"
#include "camera.h"
#include "config_cerebro.h"
#include "corpo_link.h"
#include "linha.h"
#include "microfone.h"
#include "protocolo.h"
#include "voz.h"

namespace {

cerebro::Corpo corpo;
cerebro::Audio alto_falante;
cerebro::Microfone microfone;
cerebro::Camera camera;
cerebro::VozDeMentira voz;

bool tem_audio  = false;
bool tem_mic    = false;
bool tem_camera = false;

// Alternancia das teclas 1 e 2 do console: cada toque manda o servo
// para o outro extremo, o que basta para ver se ele obedece.
bool servo1_alto = false;
bool servo2_alto = false;

// ---- Sentidos: o microfone, lido sem atrapalhar ninguem ------------
void tarefaSentidos(void*) {
  static int16_t bloco[256];
  uint32_t mudo = 0;  // leituras seguidas sem nenhuma amostra
  for (;;) {
    if (tem_mic) {
      const size_t n = microfone.le(bloco, sizeof(bloco) / sizeof(bloco[0]));
      if (n > 0) {
        if (mudo > 50) Serial.println("[sentidos] microfone voltou a dar sinal");
        mudo = 0;
        voz.ouve(bloco, n);
      } else {
        // Microfone sem nada ligado devolve timeout a cada leitura, e
        // o driver reclama sozinho no log - vinte vezes por segundo,
        // o que enterra qualquer outra mensagem. Depois de um segundo
        // assim, espaca as tentativas: o diagnostico continua, porque
        // ele volta a acusar quando o sinal voltar, e o console volta
        // a ser legivel.
        mudo++;
        if (mudo == 50) {
          Serial.println("[sentidos] microfone sem sinal - conferir a ligacao do INMP441");
        }
        if (mudo > 50) {
          vTaskDelay(pdMS_TO_TICKS(2000));
          continue;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- A rotina de teste do prompt -----------------------------------
//
//  Anda para a frente, para, e mexe os dois servos. E o menor teste
//  que prova o caminho inteiro: cerebro -> UART -> parser -> ponte H
//  e -> servo. Se ela roda, o enlace esta de pe.
//
//  Roda numa task propria e efemera para nao segurar o `loop()`: o
//  heartbeat continua correndo em paralelo, que e justamente o que se
//  quer exercitar.
void tarefaTeste(void*) {
  Serial.println("\n[teste] 1/4 - frente, 40%, por 1,5 s");
  corpo.anda(40, 40);
  vTaskDelay(pdMS_TO_TICKS(1500));

  Serial.println("[teste] 2/4 - parar");
  corpo.para();
  vTaskDelay(pdMS_TO_TICKS(800));

  Serial.println("[teste] 3/4 - girar no lugar, 1 s");
  corpo.anda(50, -50);
  vTaskDelay(pdMS_TO_TICKS(1000));
  corpo.para();
  vTaskDelay(pdMS_TO_TICKS(500));

  Serial.println("[teste] 4/4 - servos: 45, 135, 90");
  const int angulos[] = {45, 135, 90};
  for (int a : angulos) {
    corpo.servo(1, a);
    vTaskDelay(pdMS_TO_TICKS(400));
    corpo.servo(2, a);
    vTaskDelay(pdMS_TO_TICKS(400));
  }

  if (tem_audio) alto_falante.bipe(1200, 120);
  Serial.printf("[teste] fim. corpo %s, %lu enviados, %lu respostas, %lu erros\n",
                corpo.vivo() ? "respondendo" : "MUDO", (unsigned long)corpo.enviados(),
                (unsigned long)corpo.respostas(), (unsigned long)corpo.erros());
  vTaskDelete(nullptr);
}

void ajuda() {
  Serial.println("\n--- console do cerebro ---");
  Serial.println("  t          rotina de teste (anda, para, mexe os servos)");
  Serial.println("  w a s d x  frente, esquerda, re, direita, parar");
  Serial.println("  1 / 2      servo 1 / servo 2 para 45, depois 135");
  Serial.println("  b          bipe no alto-falante");
  Serial.println("  f          foto: mede um quadro da camera");
  Serial.println("  p          despeja a foto em base64 pela serial");
  Serial.println("  ?          estado de tudo");
}

void estado() {
  Serial.printf("\n=== %s v%s ===\n", CEREBRO_NOME, CEREBRO_VERSAO);
  Serial.printf("corpo:       %s (%lu ms desde a ultima resposta)\n",
                corpo.vivo() ? "respondendo" : "MUDO", (unsigned long)corpo.desde_resposta_ms());
  Serial.printf("             %lu enviados, %lu respostas, %lu recusas\n",
                (unsigned long)corpo.enviados(), (unsigned long)corpo.respostas(),
                (unsigned long)corpo.erros());
  // "FALHOU" e "nao existe nesta placa" sao coisas diferentes, e o
  // diagnostico so serve se disser qual das duas e. Numa placa sem
  // amplificador, "FALHOU" manda procurar defeito onde nao ha nada.
  Serial.printf("alto-falante:%s\n",
                tem_audio ? " ok" : (TEM_AMPLIFICADOR ? " FALHOU" : " nao ha nesta placa"));
  Serial.printf("microfone:   %s  pico recente %.3f em %lu blocos\n", tem_mic ? "ok" : "FALHOU",
                voz.picoEZera(), (unsigned long)voz.blocos());
  Serial.printf("camera:      %s\n", tem_camera ? "ok" : "FALHOU");
  Serial.printf("voz:         %s\n",
                voz.ligado() ? "ligada" : "nenhum servico (ver docs/05-a-voz.md)");
  Serial.printf("PSRAM:       %u KB livres de %u KB\n", (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(ESP.getPsramSize() / 1024));
  Serial.printf("heap:        %u KB livres\n", (unsigned)(ESP.getFreeHeap() / 1024));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t ate = millis() + 2000;
  while (!Serial && millis() < ate) delay(10);  // da tempo de o USB aparecer

  Serial.printf("\n=== %s v%s ===\n", CEREBRO_NOME, CEREBRO_VERSAO);

  // O enlace com o corpo PRIMEIRO. Se alguma coisa abaixo travar, o
  // corpo ja esta recebendo heartbeat e o robo esta parado de forma
  // deliberada, em vez de parado por failsafe.
  if (!corpo.begin()) {
    Serial.println("[cerebro] FALHA ao subir o enlace com o corpo");
  } else {
    Serial.printf("[cerebro] enlace de pe: TX=%d RX=%d, heartbeat a cada %lu ms\n", PIN_UART_TX,
                  PIN_UART_RX, (unsigned long)protocolo::HEARTBEAT_MS);
  }

  // A CAMERA PRIMEIRO, E A ORDEM E O DEFEITO QUE ISTO CONSERTA.
  //
  // No ESP32 classico a camera NAO tem controlador proprio: a
  // interface DVP roda sobre o I2S0, em modo paralelo, e so o I2S0
  // serve. Subir o microfone antes fazia ele levar o I2S0 embora, e a
  // camera morria com uma mensagem que nao fala em camera nenhuma:
  //
  //     No free interrupt inputs for I2S0 interrupt
  //     Camera config failed with error 0xffffffff
  //
  // Com a camera primeiro, ela fica com o I2S0 e o microfone cai no
  // I2S1 sozinho. Na XIAO S3 a ordem e indiferente - la a camera tem
  // controlador proprio -, entao a mesma sequencia serve nas duas.
  //
  // Achado com a placa na mesa em 29/09/2026. Nenhuma leitura de
  // datasheet tinha apontado isso.
  tem_camera = camera.begin();
  Serial.printf("[cerebro] camera: %s", tem_camera ? "ok" : "FALHOU");
  if (!tem_camera) Serial.printf(" (erro 0x%x)", camera.erro());
  Serial.println();

  tem_audio = alto_falante.begin();
  Serial.printf("[cerebro] alto-falante: %s\n",
                tem_audio ? "ok" : (TEM_AMPLIFICADOR ? "FALHOU" : "nao ha nesta placa"));

  tem_mic = microfone.begin();
  Serial.printf("[cerebro] microfone: %s\n", tem_mic ? "ok" : "FALHOU");

  voz.begin();

  xTaskCreatePinnedToCore(tarefaSentidos, "sentidos", 4096, nullptr, 2, nullptr, 1);

  if (tem_audio) alto_falante.bipe(660, 90);
  ajuda();
}

void loop() {
  // ---- Console --------------------------------------------------
  while (Serial.available()) {
    const char c = (char)Serial.read();
    switch (c) {
      case 't': xTaskCreatePinnedToCore(tarefaTeste, "teste", 4096, nullptr, 2, nullptr, 1); break;
      case 'w':
        corpo.anda(50, 50);
        Serial.println("frente");
        break;
      case 's':
        corpo.anda(-50, -50);
        Serial.println("re");
        break;
      case 'a':
        corpo.anda(-45, 45);
        Serial.println("esquerda");
        break;
      case 'd':
        corpo.anda(45, -45);
        Serial.println("direita");
        break;
      case 'x':
        corpo.para();
        Serial.println("parar");
        break;
      case '1':
        corpo.servo(1, servo1_alto ? 45 : 135);
        servo1_alto = !servo1_alto;
        break;
      case '2':
        corpo.servo(2, servo2_alto ? 45 : 135);
        servo2_alto = !servo2_alto;
        break;
      case 'b':
        if (tem_audio) alto_falante.bipe();
        break;
      case 'p':
        // Despeja a foto pela serial. Sem cartao SD e sem Wi-Fi
        // configurado, e o unico caminho para a imagem virar arquivo.
        Serial.println("[camera] capturando...");
        if (!camera.despeja(Serial)) Serial.println("[camera] falhou");
        break;
      case 'f': {
        const size_t n = camera.mede();
        Serial.printf("[camera] quadro de %u bytes\n", (unsigned)n);
        break;
      }
      case '?': estado(); break;
      case 'h': ajuda(); break;
      default: break;
    }
  }

  // ---- Um sinal de vida, sem inundar o console -------------------
  static uint32_t ultimo_relato = 0;
  if (millis() - ultimo_relato > 10000) {
    ultimo_relato = millis();
    if (!corpo.vivo()) {
      Serial.printf("[cerebro] corpo MUDO ha %lu ms - conferir o cabo da UART\n",
                    (unsigned long)corpo.desde_resposta_ms());
    }
  }

  delay(10);
}
