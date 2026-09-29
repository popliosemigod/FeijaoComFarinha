// =====================================================================
//  main_corpo.cpp - ESP32-C3 SuperMini: o corpo do Feijao com Farinha
//
//  Ele nao pensa. Recebe comando pela UART, obedece dentro do que o
//  hardware aguenta, responde OK / ERR / PONG, e PARA SOZINHO se o
//  cerebro calar por mais de um segundo.
//
//  Essa ultima frase e a razao de esta placa existir separada. O
//  cerebro tem Wi-Fi, camera e audio disputando dois nucleos: se o
//  movimento dependesse dele, um travamento de rede viraria um robo
//  andando sem ninguem no comando. Aqui, o pior caso de qualquer
//  defeito la e o robo parar.
//
//  Console pelo USB CDC. Cabo USB fora nao atrapalha: o core descarta
//  o que ninguem esta lendo, entao o log fica ligado com o robo
//  andando pela sala.
// =====================================================================
#include <Arduino.h>

#include "config_corpo.h"
#include "linha.h"
#include "motores.h"
#include "protocolo.h"
#include "servos.h"

namespace {

corpo::Motores motores;
corpo::Servos servos;
enlace::Linha entrada;

uint32_t ultimo_movimento_ms = 0;  // alimenta o failsafe
uint32_t ultimo_tick_ms      = 0;
bool failsafe_disparado      = false;

// Contadores do log. Servem para responder "ele esta recebendo?" sem
// depender de ver o momento exato passar na tela.
uint32_t comandos_ok    = 0;
uint32_t comandos_erro  = 0;
uint32_t vezes_failsafe = 0;

void responde(const char* texto) {
  Serial1.print(texto);
  Serial1.print('\n');
}

void executa(const protocolo::Comando& c) {
  switch (c.tipo) {
    case protocolo::Tipo::MOTOR: {
      const bool dentro = motores.velocidade(c.a, c.b);
      responde(protocolo::RESP_OK);
      comandos_ok++;
      if (!dentro) Serial.printf("[corpo] M fora de faixa, preso no limite\n");
      break;
    }

    case protocolo::Tipo::SERVO:
      servos.angulo((uint8_t)c.a, c.b);
      responde(protocolo::RESP_OK);
      comandos_ok++;
      break;

    case protocolo::Tipo::PARAR:
      motores.para();
      responde(protocolo::RESP_OK);
      comandos_ok++;
      Serial.println("[corpo] STOP");
      break;

    case protocolo::Tipo::HABILITA:
      motores.habilita(c.a == 1);
      responde(protocolo::RESP_OK);
      comandos_ok++;
      Serial.printf("[corpo] pontes %s\n", c.a == 1 ? "habilitadas" : "soltas");
      break;

    case protocolo::Tipo::PING:
      responde(protocolo::RESP_PONG);
      comandos_ok++;
      break;

    case protocolo::Tipo::ERRO:
      responde(c.erro);
      comandos_erro++;
      Serial.printf("[corpo] recusado: %s\n", c.erro);
      break;

    case protocolo::Tipo::NADA: break;  // linha vazia nao merece resposta nem log
  }

  // O relogio do failsafe so avanca com comando de MOVIMENTO. Servo
  // nao conta: um robo andando em linha reta que so mexe a cabeca
  // continua sendo um robo andando.
  if (c.alimentaFailsafe()) {
    ultimo_movimento_ms = millis();
    if (failsafe_disparado) {
      failsafe_disparado = false;
      Serial.println("[corpo] cerebro voltou");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(LOG_BAUD);

  // ---- Sequencia de boot segura, e a ordem e o que importa --------
  //  1. EN em LOW (dentro de motores.begin(), antes de qualquer PWM)
  //  2. configurar os PWM
  //  3. PWM em zero
  //  4. servos em 90 graus
  //  5. so entao EN em HIGH
  //
  // Inverter 1 e 2 deixa um instante com a ponte habilitada e o duty
  // ainda indefinido - e o tranco no boot que assusta quem esta com o
  // robo na mao.
  const bool ok_motores = motores.begin();
  const bool ok_servos  = servos.begin();

  Serial1.begin(protocolo::BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

  motores.habilita(true);
  ultimo_movimento_ms = millis();
  ultimo_tick_ms      = millis();

  Serial.printf("\n=== %s v%s ===\n", CORPO_NOME, CORPO_VERSAO);
  Serial.printf("PWM motores: %d Hz, %d bits%s\n", PWM_MOTOR_HZ, PWM_MOTOR_BITS,
                ok_motores ? "" : "  <-- FALHOU");
  Serial.printf("PWM servos:  %d Hz, %d bits%s\n", PWM_SERVO_HZ, PWM_SERVO_BITS,
                ok_servos ? "" : "  <-- FALHOU");
  Serial.printf("UART1: RX=%d TX=%d a %lu baud\n", PIN_UART_RX, PIN_UART_TX,
                (unsigned long)protocolo::BAUD);
  Serial.printf("failsafe: para com %lu ms sem comando de movimento\n",
                (unsigned long)protocolo::SILENCIO_MS);
  Serial.printf("rampa de subida: %s\n",
                RAMPA_SUBIDA_MS == 0 ? "desligada (comando vale na hora)" : "ligada");
  Serial.println("pronto. Comandos: M <esq> <dir> | S <n> <ang> | STOP | EN <0|1> | PING");
}

void loop() {
  // ---- Entrada: um byte por vez, sem esperar por ninguem ----------
  while (Serial1.available()) {
    if (entrada.alimenta((char)Serial1.read())) {
      executa(protocolo::interpreta(entrada.texto()));
    }
    if (entrada.estourou()) {
      responde(protocolo::ERR_LONGA);
      comandos_erro++;
      Serial.println("[corpo] linha longa demais, descartada");
    }
  }

  // ---- O mesmo pelo console, para depurar sem o cerebro -----------
  // Com o robo na bancada da para abrir o monitor e digitar `M 40 40`
  // sem nenhum software do lado do PC. Foi por isso que o protocolo
  // virou texto.
  static enlace::Linha console;
  while (Serial.available()) {
    if (console.alimenta((char)Serial.read())) {
      const protocolo::Comando c = protocolo::interpreta(console.texto());
      Serial.printf("[console] %s\n", console.texto());
      executa(c);
    }
  }

  const uint32_t agora = millis();

  // ---- Failsafe ---------------------------------------------------
  //
  //  Quem para o robo e esta placa, contando o tempo desde o ultimo
  //  comando de movimento. Nao ha condicao em que ela dependa do
  //  cerebro para isso - inclusive porque o caso que importa e
  //  justamente o cerebro ter parado de responder.
  if (!failsafe_disparado && (agora - ultimo_movimento_ms) > protocolo::SILENCIO_MS) {
    motores.para();
    failsafe_disparado = true;
    vezes_failsafe++;
    Serial.printf("[corpo] FAILSAFE: %lu ms sem comando, motores parados (%lu vez)\n",
                  (unsigned long)(agora - ultimo_movimento_ms), (unsigned long)vezes_failsafe);
  }

  // ---- Passo do movimento -----------------------------------------
  if ((agora - ultimo_tick_ms) >= PASSO_LACO_MS) {
    motores.tick(agora - ultimo_tick_ms);
    ultimo_tick_ms = agora;
  }
}
