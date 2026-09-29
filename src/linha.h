// =====================================================================
//  linha.h - juntar bytes ate virar uma linha, sem bloquear
//
//  Parser de serial que usa `readStringUntil('\n')` para tudo: quando o
//  cabo se solta no meio de um comando, ele fica esperando o resto de
//  uma linha que nunca chega, e o robo para de atender enquanto isso -
//  inclusive o proprio failsafe, que precisa do laco rodando.
//
//  Aqui cada byte entra e sai na hora. Linha completa devolve true uma
//  vez; excesso de bytes sem '\n' nao estoura memoria, marca overflow e
//  descarta ate o proximo fim de linha.
//
//  Vale para os dois lados: no corpo ele le comandos, no cerebro le as
//  respostas. E e logica pura, o que permite o autoteste exercitar
//  linha partida, linha longa e CRLF sem nenhum cabo ligado.
// =====================================================================
#pragma once

#include <stdint.h>

#include "protocolo.h"

namespace enlace {

class Linha {
public:
  // Devolve true quando `texto()` passa a conter uma linha completa,
  // ja terminada em '\0' e sem o '\n'.
  bool alimenta(char c) {
    if (c == '\r') return false;  // CRLF de terminal Windows

    if (c == '\n') {
      if (descartando_) {
        // Fim da linha longa: a partir daqui volta a valer.
        descartando_ = false;
        n_           = 0;
        estourou_    = true;
        return false;
      }
      buf_[n_] = '\0';
      pronta_  = n_;
      n_       = 0;
      return true;
    }

    if (descartando_) return false;

    if (n_ >= protocolo::LINHA_MAX - 1) {
      // Nao trunca e entrega pela metade: `M 100 10` truncado de
      // `M 100 100` e um comando VALIDO e errado. Descarta a linha
      // inteira e avisa.
      descartando_ = true;
      n_           = 0;
      return false;
    }

    buf_[n_++] = c;
    return false;
  }

  const char* texto() const { return buf_; }
  uint8_t tamanho() const { return pronta_; }

  // Consultado e zerado: quem pergunta e quem responde `ERR
  // linha-longa`, e ninguem deve responder duas vezes pelo mesmo erro.
  bool estourou() {
    const bool e = estourou_;
    estourou_    = false;
    return e;
  }

  void limpa() {
    n_           = 0;
    pronta_      = 0;
    descartando_ = false;
    estourou_    = false;
    buf_[0]      = '\0';
  }

private:
  char buf_[protocolo::LINHA_MAX] = {0};
  uint8_t n_                      = 0;
  uint8_t pronta_                 = 0;
  bool descartando_               = false;
  bool estourou_                  = false;
};

}  // namespace enlace
