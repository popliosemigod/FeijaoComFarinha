// =====================================================================
//  corpo_link.h - o lado do cerebro: falar com o corpo sem pensar nisso
//
//  Quem usa esta classe diz `corpo.anda(60, 60)` e para de se
//  preocupar. O reenvio periodico que mantem o failsafe alimentado
//  acontece numa task propria, e nao no laco de quem pediu.
//
//  Essa escolha e o ponto inteiro do arquivo. Se o heartbeat morasse
//  no `loop()`, qualquer coisa demorada no cerebro - decodificar um
//  quadro de camera, esperar uma resposta HTTP - deixaria o corpo sem
//  noticia por mais de um segundo, e o robo pararia no meio do
//  caminho. Com uma task de prioridade propria, o cerebro pode travar
//  numa rede ruim que as rodas continuam obedecendo ao ultimo comando
//  consciente.
//
//  E o contrario tambem vale, e e a parte que protege: se a TASK
//  morrer, o corpo para sozinho em um segundo. Nenhum dos dois lados
//  precisa confiar no outro.
// =====================================================================
#pragma once

#include <Arduino.h>

#include <atomic>

#include "config_cerebro.h"
#include "linha.h"
#include "protocolo.h"

namespace cerebro {

class Corpo {
public:
  bool begin() {
    Serial1.begin(protocolo::BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    trava_ = xSemaphoreCreateMutex();
    if (trava_ == nullptr) return false;

    // Nucleo 0: o 1 fica para a aplicacao (camera, audio, decisao).
    // Prioridade 3, acima do loop() (que roda em 1): o heartbeat e
    // curto e precisa acontecer na hora certa, nao quando sobrar.
    const BaseType_t r = xTaskCreatePinnedToCore(tarefa, "corpo-link", 3072, this, 3, &task_, 0);
    return r == pdPASS;
  }

  // ---- O que o resto do cerebro chama ------------------------------

  void anda(int esq, int dir) {
    trancado([&] {
      esq_ = esq;
      dir_ = dir;
    });
  }

  void para() {
    trancado([&] {
      esq_       = 0;
      dir_       = 0;
      pede_stop_ = true;
    });
  }

  void servo(uint8_t n, int graus) {
    trancado([&] {
      servo_n_    = n;
      servo_ang_  = graus;
      pede_servo_ = true;
    });
  }

  void habilita(bool ligado) {
    trancado([&] {
      en_      = ligado ? 1 : 0;
      pede_en_ = true;
      if (!ligado) {
        esq_ = 0;
        dir_ = 0;
      }
    });
  }

  // ---- O que o resto do cerebro pergunta ---------------------------

  // O corpo respondeu ha pouco? Nao e ele quem freia o robo - disso
  // cuida o failsafe do outro lado -, mas insistir em falar com quem
  // nao responde esconde o defeito do log.
  bool vivo() const { return (millis() - ultima_resposta_ms_) < CORPO_MUDO_MS; }

  uint32_t enviados() const { return enviados_; }
  uint32_t respostas() const { return respostas_; }
  uint32_t erros() const { return erros_; }
  uint32_t desde_resposta_ms() const { return millis() - ultima_resposta_ms_; }

private:
  template <typename F>
  void trancado(F f) {
    if (trava_ == nullptr) return;
    if (xSemaphoreTake(trava_, pdMS_TO_TICKS(50)) == pdTRUE) {
      f();
      xSemaphoreGive(trava_);
    }
  }

  static void tarefa(void* arg) { static_cast<Corpo*>(arg)->laco(); }

  void laco() {
    enlace::Linha resposta;
    TickType_t proximo = xTaskGetTickCount();

    for (;;) {
      // ---- Le o que o corpo respondeu ------------------------------
      while (Serial1.available()) {
        if (resposta.alimenta((char)Serial1.read())) {
          ultima_resposta_ms_ = millis();
          respostas_++;
          if (resposta.texto()[0] == 'E' && resposta.texto()[1] == 'R') {
            erros_++;
            // Recusa do corpo nao se resolve insistindo: e um comando
            // que o cerebro montou errado. Vai para o log inteira.
            Serial.printf("[corpo] %s\n", resposta.texto());
          }
        }
      }

      // ---- Manda o que esta pendente -------------------------------
      int esq = 0, dir = 0;
      bool stop = false, en = false, sv = false;
      int en_v = 1, sv_n = 1, sv_a = 90;

      trancado([&] {
        esq         = esq_;
        dir         = dir_;
        stop        = pede_stop_;
        en          = pede_en_;
        sv          = pede_servo_;
        en_v        = en_;
        sv_n        = servo_n_;
        sv_a        = servo_ang_;
        pede_stop_  = false;
        pede_en_    = false;
        pede_servo_ = false;
      });

      char linha[protocolo::LINHA_MAX];

      // EN primeiro: habilitar depois de mandar velocidade faria o
      // robo arrancar com o comando anterior ainda valendo.
      if (en) {
        const int n = snprintf(linha, sizeof(linha), "EN %d\n", en_v);
        Serial1.write((const uint8_t*)linha, n);
        enviados_++;
      }

      if (stop) {
        Serial1.write((const uint8_t*)"STOP\n", 5);
        enviados_++;
      } else {
        // O reenvio periodico E o heartbeat. Mandar `M 0 0` quando
        // parado nao e desperdicio: e o que diz ao corpo "continuo
        // aqui, e a minha intencao e ficar parado" - diferente de
        // sumir, que faz o failsafe disparar e virar log de erro.
        const int n = protocolo::montaMotor(linha, sizeof(linha), esq, dir);
        Serial1.write((const uint8_t*)linha, n);
        enviados_++;
      }

      if (sv) {
        const int n = protocolo::montaServo(linha, sizeof(linha), sv_n, sv_a);
        Serial1.write((const uint8_t*)linha, n);
        enviados_++;
      }

      // Espera absoluta, e nao `delay` relativo: assim o periodo nao
      // escorrega quando um ciclo demora mais, que e exatamente o que
      // faria o intervalo passar de um segundo sob carga.
      vTaskDelayUntil(&proximo, pdMS_TO_TICKS(protocolo::HEARTBEAT_MS));
    }
  }

  SemaphoreHandle_t trava_ = nullptr;
  TaskHandle_t task_       = nullptr;

  // Intencao atual, protegida pela trava.
  int esq_         = 0;
  int dir_         = 0;
  int en_          = 1;
  int servo_n_     = 1;
  int servo_ang_   = 90;
  bool pede_stop_  = false;
  bool pede_en_    = false;
  bool pede_servo_ = false;

  // Atomicos, e nao `volatile`. Sao escritos pela task do enlace e
  // lidos pelo `loop()`, que e outra task noutro nucleo - `volatile`
  // nunca deu essa garantia (ele fala de acesso a hardware, nao de
  // concorrencia), e em C++20 ate incrementar um deles virou aviso de
  // depreciacao. Um contador errado aqui nao trava nada, mas mentiria
  // no diagnostico, que e o unico uso que eles tem.
  std::atomic<uint32_t> ultima_resposta_ms_{0};
  std::atomic<uint32_t> enviados_{0};
  std::atomic<uint32_t> respostas_{0};
  std::atomic<uint32_t> erros_{0};
};

}  // namespace cerebro
