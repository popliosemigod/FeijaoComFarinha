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

// O log do que a mao manda. E o que prova, na bancada e sem motor
// ligado, que o manche chega nas rodas. No maximo um a cada 150 ms com
// o manche em movimento - o controle manda dezenas de relatorios por
// segundo -, mas parada e freio saem na hora.
void relataControle(int esq, int dir, bool freio) {
  static int esq_dito = 0, dir_dito = 0;
  static bool freio_dito  = false;
  static uint32_t dito_ms = 0;
  if (esq == esq_dito && dir == dir_dito && freio == freio_dito) return;
  const bool urgente = freio != freio_dito || (esq == 0 && dir == 0);
  if (!urgente && (millis() - dito_ms) < 150) return;
  esq_dito   = esq;
  dir_dito   = dir;
  freio_dito = freio;
  dito_ms    = millis();
  if (freio) {
    Serial.println("[ps4] O: freio");
  } else {
    Serial.printf("[ps4] M %d %d\n", esq, dir);
  }
}

// Manche direito nos servos 1 (lados) e 2 (cima/baixo). O manche MOVE o
// servo, a PS4_SERVO_GRAUS_POR_S no fim; solto, o servo fica onde
// parou. R3 devolve os dois ao repouso. O log segue a regra do manche
// esquerdo: no maximo um a cada 150 ms em movimento.
void mancheNosServos(int rx, int ry, bool centra) {
  static float alvo[2]        = {SERVO_REPOUSO, SERVO_REPOUSO};
  static int dito[2]          = {SERVO_REPOUSO, SERVO_REPOUSO};
  static uint32_t antes_ms    = millis();
  static uint32_t dito_ms     = 0;
  static bool centrava        = false;
  static const int sentido[2] = {PS4_SERVO_SENTIDO_1, PS4_SERVO_SENTIDO_2};

  // Um laco mais lento (ponte, gravacao) nao vira um salto do servo.
  const uint32_t dt = std::min<uint32_t>(millis() - antes_ms, 50);
  antes_ms          = millis();

  if (centra && !centrava) {
    alvo[0] = alvo[1] = SERVO_REPOUSO;
    Serial.println("[ps4] R3: servos ao repouso");
  }
  centrava = centra;

  const int v[2] = {rx, ry};
  for (uint8_t i = 0; i < 2; i++) {
    // O cerebro pode ter posto o servo em outro lugar com `S`: o manche
    // continua de onde o servo esta, e nao de onde ele o deixou.
    if (abs((int)lroundf(alvo[i]) - servos.onde(i + 1)) > 1 && !centra) {
      alvo[i] = servos.onde(i + 1);
    }
    alvo[i] =
        constrain(alvo[i] + sentido[i] * corpo::ControlePS4::passoDoServo(v[i], dt), 0.0f, 180.0f);
    const int a = (int)lroundf(alvo[i]);
    if (a != servos.onde(i + 1)) servos.angulo(i + 1, a);
  }
  if ((servos.onde(1) != dito[0] || servos.onde(2) != dito[1]) && millis() - dito_ms >= 150) {
    dito[0] = servos.onde(1);
    dito[1] = servos.onde(2);
    dito_ms = millis();
    Serial.printf("[ps4] manche direito: servo 1 em %d, servo 2 em %d\n", dito[0], dito[1]);
  }
}

// X pede uma foto ao cerebro - a camera e dele. Vai pelo caminho do
// console (`>p`), o mesmo de quem digita `>p` no USB daqui: o enlace
// ja carrega isso, e nada muda no protocolo de movimento. Uma foto por
// aperto, e no maximo uma a cada PS4_FOTO_INTERVALO_MS.
void xNaCamera(bool apertado) {
  static bool estava        = false;
  static bool ja_pediu      = false;
  static uint32_t pedido_ms = 0;
  if (apertado && !estava) {
    if (!ja_pediu || millis() - pedido_ms >= PS4_FOTO_INTERVALO_MS) {
      ja_pediu  = true;
      pedido_ms = millis();
      Serial1.printf("%cp\n", protocolo::MARCA_CONSOLE);
      Serial.println("[ps4] X: foto pedida ao cerebro");
    } else {
      Serial.println("[ps4] X: ignorado, a foto anterior ainda esta saindo");
    }
  }
  estava = apertado;
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

// `PAREIA aa:bb:cc:dd:ee:ff`, so pelo USB: o controle que esta placa
// passa a aceitar. Quem manda e `scripts/pareia_ps4.py --corpo`, logo
// depois de gravar no controle o endereco daqui - os dois lados do
// pareamento de uma vez, como o PS4 faz pelo cabo. Devolve o resto da
// linha, ou nullptr se a linha nao e esta.
const char* ehPareia(const char* linha) {
  static const char ALVO[] = "PAREIA ";
  for (uint8_t i = 0; i < sizeof(ALVO) - 1; i++) {
    char c = linha[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c != ALVO[i]) return nullptr;
  }
  return linha + sizeof(ALVO) - 1;
}

void pareia(const char* mac) {
  if (!corpo::ControlePS4::lembra(mac)) {
    Serial.printf("[ps4] PAREIA recusado: %s\n", mac);
    return;
  }
  // A chave entra na pilha do Bluetooth no boot, antes de a placa
  // atender - entao reinicia. Motor parado antes, por educacao.
  motores.para();
  Serial.printf("[ps4] controle %s guardado, reiniciando\n", mac);
  Serial.flush();
  delay(50);
  ESP.restart();
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
    Serial.printf("[ps4] Endereco desta placa: %s\n", controle.endereco().c_str());
    if (controle.conhecido().length() > 0) {
      Serial.printf("[ps4] esperando o controle %s\n", controle.conhecido().c_str());
    } else {
      Serial.println(
          "[ps4] nenhum controle pareado: scripts/pareia_ps4.py --corpo <porta>,"
          " com o controle no USB do PC");
    }
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
        Serial.printf("[cerebro] %s\n", entrada.texto() + 1);
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
      } else if (const char* mac = ehPareia(console.texto())) {
        pareia(mac);
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
        // A mao sumiu no meio do movimento. Os servos ficam onde estao,
        // como ficariam com o manche direito solto.
        motores.para();
        controle_dirigindo = false;
      }
    }

    if (conectado && controle.falando()) {
      const corpo::ControlePS4::Mao mao = controle.le();
      mancheNosServos(mao.rx, mao.ry, mao.centra);
      xNaCamera(mao.foto);
      if (mao.freio || mao.mexeu) {
        if (mao.freio) {
          motores.para();
          relataControle(0, 0, true);
        } else {
          motores.velocidade(mao.esq, mao.dir);
          relataControle(mao.esq, mao.dir, false);
        }
        controle_dirigindo  = true;
        controle_toque_ms   = agora;
        ultimo_movimento_ms = agora;  // o controle tambem alimenta o failsafe
        failsafe_disparado  = false;
      } else if (controle_dirigindo) {
        // Manche de volta ao centro: o robo para, e o cerebro so
        // retoma depois de PS4_PRIORIDADE_MS.
        motores.velocidade(0, 0);
        relataControle(0, 0, false);
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
