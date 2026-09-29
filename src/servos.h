// =====================================================================
//  servos.h - os dois servos auxiliares
//
//  Servo por LEDC direto, sem biblioteca. Sao duas contas e dois
//  registradores; uma dependencia a mais so traria o risco de ela
//  brigar com o LEDC dos motores pelos mesmos canais - e no C3 os seis
//  canais estao todos ocupados.
//
//  A conta: em 50 Hz o periodo e 20000 us. Com 14 bits, o duty cheio
//  (16383) vale esses 20000 us, entao 1 us custa 16383/20000 = 0,819
//  contagem. Um pulso de 1500 us vira duty 1229.
//
//  Por que 14 bits: e o MAXIMO do LEDC do C3, e da passo de 1,2 us.
//  Com 10 bits o passo seria 19,5 us - cerca de 1,7 grau, visivel como
//  tranco em movimento lento.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "config_corpo.h"

namespace corpo {

class Servos {
public:
  // Posicao inicial em 90 graus ANTES de o EN das pontes subir. Um
  // servo sem pulso definido vai para onde estiver e puxa corrente
  // fazendo isso; melhor que ele ja tenha destino quando a fonte ainda
  // nao esta alimentando dois motores.
  bool begin() {
    bool ok = true;
    ok &= ledcAttach(PIN_SERVO_1, PWM_SERVO_HZ, PWM_SERVO_BITS);
    ok &= ledcAttach(PIN_SERVO_2, PWM_SERVO_HZ, PWM_SERVO_BITS);
    angulo(1, SERVO_REPOUSO);
    angulo(2, SERVO_REPOUSO);
    return ok;
  }

  // O unico caminho ate o hardware. `n` e 1 ou 2, como no protocolo -
  // nao e indice de array, e nao vira um por engano em lugar nenhum.
  bool angulo(uint8_t n, int graus) {
    if (n < 1 || n > protocolo::SERVOS) return false;
    bool dentro = true;
    if (graus < protocolo::ANG_MIN) {
      graus  = protocolo::ANG_MIN;
      dentro = false;
    }
    if (graus > protocolo::ANG_MAX) {
      graus  = protocolo::ANG_MAX;
      dentro = false;
    }
    onde_[n - 1]    = (int16_t)graus;
    const uint8_t p = (n == 1) ? PIN_SERVO_1 : PIN_SERVO_2;
    ledcWrite(p, dutyDoAngulo(graus));
    return dentro;
  }

  int16_t onde(uint8_t n) const { return (n >= 1 && n <= protocolo::SERVOS) ? onde_[n - 1] : 0; }

  // Corta o pulso: o servo fica solto, nao aquece e nao segura carga.
  // E o estado certo para ficar parado muito tempo, e e o que o corpo
  // faz quando decide que o cerebro sumiu de vez.
  void solta() {
    ledcWrite(PIN_SERVO_1, 0);
    ledcWrite(PIN_SERVO_2, 0);
  }

  void repouso() {
    angulo(1, SERVO_REPOUSO);
    angulo(2, SERVO_REPOUSO);
  }

  // Exposta para o autoteste: e a conta que vale a pena conferir sem
  // osciloscopio, porque errar nela queima servo contra o batente.
  static uint32_t dutyDoAngulo(int graus) {
    const long us =
        SERVO_PULSO_MIN + ((long)(SERVO_PULSO_MAX - SERVO_PULSO_MIN) * (long)graus) / 180L;
    const long cheio   = (1L << PWM_SERVO_BITS) - 1;
    const long periodo = 1000000L / PWM_SERVO_HZ;
    return (uint32_t)((us * cheio) / periodo);
  }

private:
  int16_t onde_[protocolo::SERVOS] = {SERVO_REPOUSO, SERVO_REPOUSO};
};

}  // namespace corpo
