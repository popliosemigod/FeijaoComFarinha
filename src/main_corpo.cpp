// =====================================================================
//  main_corpo.cpp - o corpo do Feijao com Farinha (ESP32 DevKit)
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
//  DUAS VOZES, E UMA REGRA ENTRE ELAS. O corpo obedece ao cerebro pela
//  UART e ao controle de PS4 pelo Bluetooth. Enquanto alguem mexe no
//  controle - e ate PS4_PRIORIDADE_MS depois do ultimo toque -, o `M`
//  do cerebro e respondido com OK e NAO move nada: a mao na manopla
//  ganha da decisao automatica. `STOP` vale sempre, venha de quem vier:
//  parar nunca espera a vez. E o controle passa pelo mesmo failsafe -
//  radio caindo sem aviso para o robo em 1 s, como cerebro calado.
//
//  Console pelo USB da placa. Cabo USB fora nao atrapalha: o log fica
//  ligado com o robo andando pela sala.
// =====================================================================
#include <Arduino.h>

#include "config_corpo.h"
#include "controle_ps4.h"
#include "linha.h"
#include "motores.h"
#include "protocolo.h"
#include "servos.h"

#if FEIJAO_PS4_LIGADO
// O core 3.x LIBERA a memoria do Bluetooth no boot, antes do setup(), a
// menos que alguem diga que vai usa-lo - e a biblioteca do PS4 nao diz.
// Sem isto o controle falha com `initialize controller failed:
// ESP_ERR_INVALID_STATE`, que nao menciona memoria nenhuma. Visto na
// bancada em 05/10/2026, no primeiro boot com o controle.
extern "C" bool btInUse() {
  return true;
}
#endif

namespace {

corpo::Motores motores;
corpo::Servos servos;
corpo::ControlePS4 controle;
enlace::Linha entrada;

bool tem_controle          = false;
bool controle_estava       = false;  // conectado na volta anterior do loop
bool controle_dirigindo    = false;  // o ultimo movimento foi dele
uint32_t controle_toque_ms = 0;      // ultimo instante com manche fora do centro

// O controle tem a prioridade enquanto esta sendo usado.
bool controleManda() {
  return controle_estava && (millis() - controle_toque_ms) < PS4_PRIORIDADE_MS;
}

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
      if (controleManda()) {
        // Aceito e ignorado: quem dirige agora e a mao no controle.
        // OK, e nao ERR, porque o cerebro nao errou nada - so perdeu a
        // vez, e um ERR viraria log de defeito do lado de la.
        responde(protocolo::RESP_OK);
        comandos_ok++;
        break;
      }
      controle_dirigindo = false;
      const bool dentro  = motores.velocidade(c.a, c.b);
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
      controle_dirigindo = false;
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

// ---- A ponte: o USB do corpo vira o cabo serial do cerebro ---------
//
//  A ESP32-CAM nao tem USB, e o adaptador dela ocupa o header inteiro:
//  com ele encaixado, microfone e enlace ficam de fora. Entao o unico
//  fio que continua ligado na CAM com o robo montado e este enlace - e
//  e por ele que passa a gravacao de firmware novo (ver
//  `atualiza_serial.h` e `scripts/grava_pelo_enlace.py`).
//
//  Enquanto a ponte esta aberta o corpo NAO interpreta nada: os bytes
//  passam crus nos dois sentidos, e um bloco de firmware pode conter
//  `M 100 100` por acaso. Por isso as pontes H ficam SOLTAS durante
//  ela inteira, e so voltam ao estado anterior quando ela fecha.
//
//  Fecha sozinha: PONTE_OCIOSA_MS sem byte nenhum vindo do USB.
void ponte() {
  const bool estava = motores.estaHabilitado();
  motores.habilita(false);
  Serial.println("[corpo] ponte aberta - motores soltos");

  uint8_t buf[256];
  uint32_t ultimo_do_usb = millis();
  while (millis() - ultimo_do_usb < PONTE_OCIOSA_MS) {
    int n = Serial.available();
    if (n > 0) {
      n = (int)Serial.readBytes(buf, (size_t)min(n, (int)sizeof(buf)));
      Serial1.write(buf, (size_t)n);
      ultimo_do_usb = millis();
    }
    n = Serial1.available();
    if (n > 0) {
      n = (int)Serial1.readBytes(buf, (size_t)min(n, (int)sizeof(buf)));
      Serial.write(buf, (size_t)n);
    }
  }

  // O que sobrou pela metade no leitor de linha e lixo binario.
  entrada.limpa();
  motores.habilita(estava);
  ultimo_movimento_ms = millis();
  Serial.println("\n[corpo] ponte fechada");
}

bool ehPonte(const char* linha) {
  static const char ALVO[] = "PONTE";
  for (uint8_t i = 0; i < sizeof(ALVO); i++) {
    char c = linha[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c != ALVO[i]) return false;
  }
  return true;
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
  Serial.println("so no USB: `>tecla` fala com o console do cerebro | PONTE abre o enlace cru");

  // O Bluetooth por ultimo: o corpo ja obedece ao cerebro antes de o
  // controle existir, e um Bluetooth que nao sobe nao derruba nada.
  tem_controle = controle.begin();
  if (tem_controle) {
    Serial.printf("[ps4] esperando o controle. Endereco desta placa: %s\n",
                  controle.endereco().c_str());
    Serial.println("[ps4] parear: scripts/pareia_ps4.py, com o controle no USB do PC");
  } else if (TEM_PS4) {
    Serial.println("[ps4] FALHOU ao subir o Bluetooth - seguindo so com o cerebro");
  }
}

void loop() {
  // ---- Entrada: um byte por vez, sem esperar por ninguem ----------
  while (Serial1.available()) {
    if (entrada.alimenta((char)Serial1.read())) {
      // Linha que comeca com '#' e RELATO do cerebro, nao comando: vai
      // para o USB e nao recebe resposta nem alimenta o failsafe. E o
      // que deixa ler o cerebro com o robo montado, pelo unico USB que
      // sobra - o do corpo.
      if (entrada.texto()[0] == protocolo::MARCA_RELATO) {
        Serial.printf("[cam] %s\n", entrada.texto() + 1);
      } else {
        executa(protocolo::interpreta(entrada.texto()));
      }
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
      if (console.texto()[0] == protocolo::MARCA_CONSOLE) {
        // `>...` nao e para o corpo: segue inteira para o cerebro, que
        // trata o resto da linha como digitado no console dele.
        Serial1.print(console.texto());
        Serial1.print('\n');
      } else if (ehPonte(console.texto())) {
        ponte();
      } else {
        const protocolo::Comando c = protocolo::interpreta(console.texto());
        Serial.printf("[console] %s\n", console.texto());
        executa(c);
      }
    }
  }

  const uint32_t agora = millis();

  // ---- Controle de PS4 --------------------------------------------
  if (tem_controle) {
    const bool conectado = controle.conectado();
    if (conectado != controle_estava) {
      controle_estava = conectado;
      Serial.printf("[ps4] controle %s\n", conectado ? "conectado" : "DESCONECTADO");
      if (conectado) {
        controle.acende(0, 60, 0);  // verde: o robo esta ouvindo
      } else if (controle_dirigindo) {
        motores.para();  // a mao sumiu no meio do movimento
        controle_dirigindo = false;
      }
    }

    if (conectado && controle.falando()) {
      int esq = 0, dir = 0;
      bool freio       = false;
      const bool mexeu = controle.le(esq, dir, freio);
      if (freio || mexeu) {
        if (freio) {
          motores.para();
        } else {
          motores.velocidade(esq, dir);
        }
        controle_dirigindo  = true;
        controle_toque_ms   = agora;
        ultimo_movimento_ms = agora;  // o controle tambem alimenta o failsafe
        failsafe_disparado  = false;
      } else if (controle_dirigindo) {
        // Manche de volta ao centro: o robo para, e o cerebro so
        // retoma depois de PS4_PRIORIDADE_MS.
        motores.velocidade(0, 0);
        controle_dirigindo = false;
      }
    }
  }

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
