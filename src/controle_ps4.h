// =====================================================================
//  controle_ps4.h - o controle de PS4, traduzido para o corpo
//
//  O controle e a segunda voz que o corpo ouve, e a unica que nao vem
//  do cerebro. Ele fala Bluetooth classico direto com esta placa: o
//  cerebro nem fica sabendo, e por isso o controle continua valendo
//  com o cerebro travado, desligado ou regravando a si proprio.
//
//  O mapeamento e o menor que se dirige sem manual:
//
//    manche esquerdo   frente/tras anda, esquerda/direita vira
//    X (cruz)          freia, enquanto apertado
//
//  A conta que transforma o manche em duas rodas (`mistura`) e pura e
//  fica fora do bloco do Bluetooth: o autoteste a exercita numa placa
//  sem controle nenhum.
// =====================================================================
#pragma once

#include <Arduino.h>

#include <atomic>

#include "config_corpo.h"
#include "protocolo.h"

#if TEM_PS4 && !defined(FEIJAO_AUTOTESTE)
#include <PS4Controller.h>
#define FEIJAO_PS4_LIGADO 1
#else
#define FEIJAO_PS4_LIGADO 0
#endif

namespace corpo {

class ControlePS4 {
public:
  // ---- A conta, sem hardware ---------------------------------------
  //
  // `x` e `y` de -127 a 127, com y positivo PARA A FRENTE. Devolve a
  // velocidade das duas rodas, de -100 a 100, no estilo "arcade": somar
  // a virada numa roda e tirar da outra. Dentro do raio morto o manche
  // vale zero - e devolve false, para quem chama saber que ele esta no
  // centro.
  static bool mistura(int x, int y, int& esq, int& dir) {
    if (abs(x) < PS4_RAIO_MORTO) x = 0;
    if (abs(y) < PS4_RAIO_MORTO) y = 0;
    esq = limita(((y + x) * 100) / 127);
    dir = limita(((y - x) * 100) / 127);
    return x != 0 || y != 0;
  }

#if FEIJAO_PS4_LIGADO
  // Sobe o Bluetooth. Com PS4_MAC vazio a placa usa o proprio endereco,
  // e e esse que `scripts/pareia_ps4.py` grava no controle.
  bool begin() {
    PS4.attach(&ControlePS4::aoRelatorio);
    return PS4_MAC[0] != '\0' ? PS4.begin(PS4_MAC) : PS4.begin();
  }

  String endereco() { return PS4.getAddress(); }
  bool conectado() { return PS4.isConnected(); }

  // Ha relatorio novo chegando? O controle manda dezenas por segundo
  // enquanto esta ligado; silencio aqui sem aviso de desconexao e
  // radio caindo - e quem cuida disso e o failsafe.
  bool falando() const { return (millis() - ultimo_ms_) < 200; }
  uint32_t relatorios() const { return relatorios_; }

  // Le o estado atual. True se o manche esta fora do centro.
  bool le(int& esq, int& dir, bool& freio) {
    freio = PS4.Cross();
    return mistura(PS4.LStickX(), -PS4.LStickY(), esq, dir);
  }

  // A barra de luz diz que o robo esta ouvindo o controle.
  void acende(uint8_t r, uint8_t g, uint8_t b) {
    PS4.setLed(r, g, b);
    PS4.sendToController();
  }
#else
  bool begin() { return false; }
  String endereco() { return String(); }
  bool conectado() { return false; }
  bool falando() const { return false; }
  uint32_t relatorios() const { return 0; }
  bool le(int& esq, int& dir, bool& freio) {
    esq = dir = 0;
    freio     = false;
    return false;
  }
  void acende(uint8_t, uint8_t, uint8_t) {}
#endif

private:
  static int limita(int v) {
    if (v > protocolo::VEL_MAX) return protocolo::VEL_MAX;
    if (v < protocolo::VEL_MIN) return protocolo::VEL_MIN;
    return v;
  }

  // Chamado pela task do Bluetooth, a cada relatorio do controle.
  static void aoRelatorio() {
    ultimo_ms_ = millis();
    relatorios_++;
  }

  static inline std::atomic<uint32_t> ultimo_ms_{0};
  static inline std::atomic<uint32_t> relatorios_{0};
};

}  // namespace corpo
