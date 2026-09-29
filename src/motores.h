// =====================================================================
//  motores.h - as duas pontes BTS7960, e o que impede que elas facam
//              besteira
//
//  Duas responsabilidades, e so estas duas:
//
//    1. traduzir "velocidade -100..100" em duty cycle de RPWM/LPWM;
//    2. garantir que nenhum caminho chegue ao hardware sem passar por
//       um limite e pelo estado do EN.
//
//  A segunda e a razao de o arquivo existir separado, e e a mesma
//  decisao que o corpo do Jaspy tomou com os servos: `escreve()` e
//  privado, e todo mundo entra por `velocidade()`. Limite que depende
//  de alguem lembrar de conferir nao e limite.
//
//  Sobre o BTS7960, que nao e obvio no datasheet:
//
//  - Sentido e velocidade saem do DUTY, nunca do EN. Para frente e
//    RPWM com duty e LPWM em zero; para tras e o contrario.
//  - Com EN alto e os dois PWM em zero, as duas saidas ficam no GND: o
//    motor freia. E isso que o `STOP` faz, e e o que se quer de um
//    comando chamado parar.
//  - Com EN baixo as saidas ficam em alta impedancia: o motor fica
//    SOLTO e o robo escorrega ate parar. E outro estado, e e por isso
//    que `EN 0` existe separado do `STOP`.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "config_corpo.h"

namespace corpo {

class Motores {
public:
  // Sequencia de boot segura, na ordem, e a ordem importa: EN baixo
  // ANTES de configurar qualquer PWM. Configurar primeiro deixaria um
  // instante com a ponte habilitada e o duty ainda indefinido, que e
  // quando o robo da o tranco que assusta quem esta segurando ele.
  bool begin() {
    pinMode(PIN_MOTOR_EN, OUTPUT);
    digitalWrite(PIN_MOTOR_EN, LOW);

    bool ok = true;
    ok &= ledcAttach(PIN_MOTOR_ESQ_R, PWM_MOTOR_HZ, PWM_MOTOR_BITS);
    ok &= ledcAttach(PIN_MOTOR_ESQ_L, PWM_MOTOR_HZ, PWM_MOTOR_BITS);
    ok &= ledcAttach(PIN_MOTOR_DIR_R, PWM_MOTOR_HZ, PWM_MOTOR_BITS);
    ok &= ledcAttach(PIN_MOTOR_DIR_L, PWM_MOTOR_HZ, PWM_MOTOR_BITS);

    zeraTudo();
    return ok;
  }

  // O UNICO jeito de pedir movimento. Fora da faixa ele NAO recusa
  // calado e tambem nao obedece ao numero errado: prende no limite e
  // avisa quem chamou, para que o log registre a diferenca entre o que
  // foi pedido e o que foi feito.
  bool velocidade(int esq, int dir) {
    bool dentro = true;
    if (esq < protocolo::VEL_MIN) {
      esq    = protocolo::VEL_MIN;
      dentro = false;
    }
    if (esq > protocolo::VEL_MAX) {
      esq    = protocolo::VEL_MAX;
      dentro = false;
    }
    if (dir < protocolo::VEL_MIN) {
      dir    = protocolo::VEL_MIN;
      dentro = false;
    }
    if (dir > protocolo::VEL_MAX) {
      dir    = protocolo::VEL_MAX;
      dentro = false;
    }
    alvo_esq_ = esq;
    alvo_dir_ = dir;
    return dentro;
  }

  // Freia: os dois lados em zero com a ponte ainda habilitada.
  // Zera tambem o alvo - senao a rampa, quando ligada, arrastaria o
  // robo de volta ao movimento no tick seguinte.
  void para() {
    alvo_esq_ = alvo_dir_ = 0;
    atual_esq_ = atual_dir_ = 0;
    aplica();
  }

  // EN: habilita ou solta as pontes. Desabilitar TAMBEM zera o PWM,
  // como o protocolo promete. Sem isso, um `EN 0` seguido de `EN 1`
  // faria o robo voltar a andar sozinho na velocidade antiga - o tipo
  // de surpresa que so aparece com o robo no chao.
  void habilita(bool ligado) {
    habilitado_ = ligado;
    if (!ligado) {
      alvo_esq_ = alvo_dir_ = 0;
      atual_esq_ = atual_dir_ = 0;
      zeraTudo();
    }
    digitalWrite(PIN_MOTOR_EN, ligado ? HIGH : LOW);
  }

  // Chamado a cada PASSO_LACO_MS. Com RAMPA_SUBIDA_MS em zero isto e
  // so uma copia do alvo para o atual - o comando vale na hora, como o
  // protocolo diz. Com rampa ligada, sobe devagar e DESCE NA HORA:
  // atrasar a subida protege a fonte, atrasar a descida atrasaria uma
  // parada de emergencia, e essas duas coisas nao se compensam.
  //
  // A escolha entre os dois casos e do PRE-PROCESSADOR, e nao de um
  // `if`. Com um `if` normal o ramo da rampa continua sendo compilado
  // mesmo desligado, e o compilador acusa - com razao - divisao por
  // zero em `100 * dt / RAMPA_SUBIDA_MS`. Aviso que se aprende a
  // ignorar deixa de ser aviso, entao o ramo morto simplesmente nao
  // entra no binario.
  void tick(uint32_t dt_ms) {
#if RAMPA_SUBIDA_MS == 0
    (void)dt_ms;
    atual_esq_ = alvo_esq_;
    atual_dir_ = alvo_dir_;
#else
    const int passo = (int)((100L * (long)dt_ms) / (long)RAMPA_SUBIDA_MS) + 1;
    atual_esq_      = aproxima(atual_esq_, alvo_esq_, passo);
    atual_dir_      = aproxima(atual_dir_, alvo_dir_, passo);
#endif
    aplica();
  }

  int esquerdo() const { return atual_esq_; }
  int direito() const { return atual_dir_; }
  bool estaHabilitado() const { return habilitado_; }
  bool parado() const { return atual_esq_ == 0 && atual_dir_ == 0; }

  // Aproxima em modulo, mas deixa a descida instantanea: o que a rampa
  // protege e o pico de partida, nao a frenagem.
  //
  // Publica e estatica para que o autoteste possa exercita-la sem
  // instanciar a classe - conferir esta conta nao pode exigir uma
  // ponte H ligada, senao ela so seria conferida tarde demais.
  static int aproxima(int atual, int alvo, int passo) {
    if (abs(alvo) < abs(atual) || (alvo > 0) != (atual > 0)) return alvo;
    if (alvo > atual) return (atual + passo > alvo) ? alvo : atual + passo;
    if (alvo < atual) return (atual - passo < alvo) ? alvo : atual - passo;
    return atual;
  }

private:
  void aplica() {
    if (!habilitado_) {
      zeraTudo();
      return;
    }
    escreve(PIN_MOTOR_ESQ_R, PIN_MOTOR_ESQ_L, atual_esq_);
    escreve(PIN_MOTOR_DIR_R, PIN_MOTOR_DIR_L, atual_dir_);
  }

  // O unico lugar do firmware que fala com o LEDC dos motores.
  static void escreve(uint8_t pino_r, uint8_t pino_l, int vel) {
    const uint32_t duty = (uint32_t)((long)abs(vel) * PWM_MOTOR_MAX / 100L);
    if (vel > 0) {
      ledcWrite(pino_l, 0);
      ledcWrite(pino_r, duty);
    } else if (vel < 0) {
      ledcWrite(pino_r, 0);
      ledcWrite(pino_l, duty);
    } else {
      ledcWrite(pino_r, 0);
      ledcWrite(pino_l, 0);
    }
  }

  static void zeraTudo() {
    ledcWrite(PIN_MOTOR_ESQ_R, 0);
    ledcWrite(PIN_MOTOR_ESQ_L, 0);
    ledcWrite(PIN_MOTOR_DIR_R, 0);
    ledcWrite(PIN_MOTOR_DIR_L, 0);
  }

  int alvo_esq_    = 0;
  int alvo_dir_    = 0;
  int atual_esq_   = 0;
  int atual_dir_   = 0;
  bool habilitado_ = false;
};

}  // namespace corpo
