// =====================================================================
//  main_autoteste.cpp - a logica das duas placas, medida numa placa nua
//
//  Grava em qualquer ESP32 que esteja na mesa, sem ponte H, sem servo,
//  sem cabo entre as placas, sem robo montado. Exercita o que da para
//  errar em silencio:
//
//    - o parser, com linha suja, CRLF, minuscula e numero fora de faixa
//    - a regra do failsafe (servo NAO alimenta o relogio)
//    - o leitor de linha, com linha partida em pedacos e linha longa
//    - a montagem dos comandos do lado do cerebro, com saturacao
//    - a conta de pulso do servo, que errada queima servo no batente
//    - a rampa, se e quando ela for ligada
//
//  Por que isto existe antes do hardware: o robo ainda nao esta
//  montado, e a alternativa seria escrever tudo e so descobrir os
//  erros no dia em que a bancada estiver ocupada com solda. Aqui cada
//  numero e verificado agora, e o que sobrar para a bancada e
//  eletrico de verdade.
// =====================================================================
#include <Arduino.h>

#include "config_corpo.h"
#include "controle_ps4.h"
#include "linha.h"
#include "motores.h"
#include "protocolo.h"
#include "servos.h"

namespace {

uint32_t passaram = 0;
uint32_t falharam = 0;

void confere(const char* nome, bool ok) {
  Serial.printf("  [%s] %s\n", ok ? "ok " : "NAO", nome);
  if (ok) {
    passaram++;
  } else {
    falharam++;
  }
}

void confereInt(const char* nome, long obtido, long esperado) {
  const bool ok = (obtido == esperado);
  Serial.printf("  [%s] %s  (obtido %ld, esperado %ld)\n", ok ? "ok " : "NAO", nome, obtido,
                esperado);
  if (ok) {
    passaram++;
  } else {
    falharam++;
  }
}

// ---------------------------------------------------------------- 1
void secaoParser() {
  using namespace protocolo;
  Serial.println("\n[1] o parser");

  Comando c = interpreta("M 70 -70");
  confere("M 70 -70 vira MOTOR", c.tipo == Tipo::MOTOR && c.a == 70 && c.b == -70);

  c = interpreta("  m   40   40  ");
  confere("minuscula e espaco sobrando", c.tipo == Tipo::MOTOR && c.a == 40 && c.b == 40);

  c = interpreta("M 0 0\r");
  confere("CRLF de terminal Windows", c.tipo == Tipo::MOTOR && c.a == 0 && c.b == 0);

  c = interpreta("S 2 180");
  confere("S 2 180 vira SERVO", c.tipo == Tipo::SERVO && c.a == 2 && c.b == 180);

  c = interpreta("STOP");
  confere("STOP", c.tipo == Tipo::PARAR);
  c = interpreta("stop");
  confere("stop em minuscula", c.tipo == Tipo::PARAR);
  c = interpreta("PING");
  confere("PING", c.tipo == Tipo::PING);
  c = interpreta("EN 0");
  confere("EN 0", c.tipo == Tipo::HABILITA && c.a == 0);

  c = interpreta("");
  confere("linha vazia nao e erro", c.tipo == Tipo::NADA);

  c = interpreta("M 200 0");
  confere("velocidade acima de 100 e recusada", c.tipo == Tipo::ERRO && c.erro == ERR_FAIXA);
  c = interpreta("M -101 0");
  confere("velocidade abaixo de -100 e recusada", c.tipo == Tipo::ERRO);
  c = interpreta("S 3 90");
  confere("servo 3 nao existe", c.tipo == Tipo::ERRO && c.erro == ERR_FAIXA);
  c = interpreta("S 1 181");
  confere("angulo 181 e recusado", c.tipo == Tipo::ERRO);
  c = interpreta("EN 2");
  confere("EN 2 nao existe", c.tipo == Tipo::ERRO);
  c = interpreta("M 40");
  confere("M com um argumento so e recusado", c.tipo == Tipo::ERRO && c.erro == ERR_ARGS);
  c = interpreta("M abc def");
  confere("M com texto no lugar de numero", c.tipo == Tipo::ERRO);
  c = interpreta("ANDA");
  confere("comando inventado e recusado", c.tipo == Tipo::ERRO && c.erro == ERR_COMANDO);
  c = interpreta("STOPX");
  confere("STOPX nao e STOP", c.tipo == Tipo::ERRO);

  // A que mais importa do bloco: `M 0 0` e um comando valido que zera
  // os motores E alimenta o failsafe. Confundir isso com "nada" faria
  // o robo parado ser considerado abandonado.
  c = interpreta("M 0 0");
  confere("M 0 0 e comando, nao silencio", c.tipo == Tipo::MOTOR && c.alimentaFailsafe());
}

// ---------------------------------------------------------------- 2
void secaoFailsafe() {
  using namespace protocolo;
  Serial.println("\n[2] a regra do failsafe");

  confere("M alimenta", interpreta("M 10 10").alimentaFailsafe());
  confere("STOP alimenta", interpreta("STOP").alimentaFailsafe());
  confere("PING alimenta", interpreta("PING").alimentaFailsafe());
  confere("EN alimenta", interpreta("EN 1").alimentaFailsafe());

  // Esta e a linha que da sentido ao resto. Um robo andando em linha
  // reta que so mexe a cabeca continua sendo um robo andando: se o
  // enlace de movimento morreu, ele tem que parar.
  confere("SERVO NAO alimenta", !interpreta("S 1 90").alimentaFailsafe());
  confere("erro NAO alimenta", !interpreta("XYZ").alimentaFailsafe());
  confere("linha vazia NAO alimenta", !interpreta("").alimentaFailsafe());

  // A folga entre os dois prazos e o que impede o robo de tremer.
  confere("heartbeat cabe pelo menos 3x no silencio", SILENCIO_MS >= HEARTBEAT_MS * 3);
}

// ---------------------------------------------------------------- 3
void secaoLinha() {
  Serial.println("\n[3] o leitor de linha");

  enlace::Linha L;
  bool pronta = false;
  for (const char* p = "M 50 50\n"; *p; p++) pronta = L.alimenta(*p);
  confere("linha completa e entregue", pronta);
  confere("conteudo sem o \\n", strcmp(L.texto(), "M 50 50") == 0);

  // Chegada em pedacos: e o caso real de uma UART a 115200 lida a
  // cada volta de um laco rapido.
  L.limpa();
  confere("pedaco 1 nao entrega", !L.alimenta('S'));
  confere("pedaco 2 nao entrega", !L.alimenta(' '));
  L.alimenta('1');
  L.alimenta(' ');
  L.alimenta('9');
  L.alimenta('0');
  confere("so o \\n entrega", L.alimenta('\n'));
  confere("montou certo", strcmp(L.texto(), "S 1 90") == 0);

  L.limpa();
  L.alimenta('P');
  L.alimenta('I');
  L.alimenta('N');
  L.alimenta('G');
  L.alimenta('\r');
  confere("\\r sozinho nao entrega nem suja", L.alimenta('\n'));
  confere("PING limpo", strcmp(L.texto(), "PING") == 0);

  // Linha longa: o perigo nao e estourar memoria, e ENTREGAR PELA
  // METADE. `M 100 100` truncado em `M 100 10` e um comando valido e
  // errado - o robo andaria torto por causa de um byte perdido.
  L.limpa();
  bool entregou = false;
  for (int i = 0; i < protocolo::LINHA_MAX + 20; i++) {
    if (L.alimenta('A')) entregou = true;
  }
  confere("linha longa nao entrega nada", !entregou);
  L.alimenta('\n');
  confere("e avisa que estourou", L.estourou());
  confere("o aviso nao se repete", !L.estourou());

  entregou = false;
  for (const char* p = "PING\n"; *p; p++) entregou = L.alimenta(*p);
  confere("volta ao normal na linha seguinte", entregou && strcmp(L.texto(), "PING") == 0);
}

// ---------------------------------------------------------------- 4
void secaoMontagem() {
  Serial.println("\n[4] a montagem, do lado do cerebro");

  char b[protocolo::LINHA_MAX];
  protocolo::montaMotor(b, sizeof(b), 60, -60);
  confere("M 60 -60", strcmp(b, "M 60 -60\n") == 0);

  protocolo::montaMotor(b, sizeof(b), 500, -500);
  confere("satura em 100 e -100", strcmp(b, "M 100 -100\n") == 0);

  protocolo::montaServo(b, sizeof(b), 1, 999);
  confere("angulo satura em 180", strcmp(b, "S 1 180\n") == 0);

  // Ida e volta: o que o cerebro escreve, o corpo le igual. E o unico
  // teste que prova que as duas metades do protocolo concordam.
  protocolo::montaMotor(b, sizeof(b), -33, 77);
  enlace::Linha L;
  bool pronta = false;
  for (const char* p = b; *p; p++) pronta = L.alimenta(*p);
  const protocolo::Comando c = protocolo::interpreta(L.texto());
  confere("ida e volta cerebro -> corpo",
          pronta && c.tipo == protocolo::Tipo::MOTOR && c.a == -33 && c.b == 77);
}

// ---------------------------------------------------------------- 5
void secaoServo() {
  Serial.println("\n[5] a conta de pulso do servo");

  // Em 50 Hz o periodo e 20000 us; com 14 bits o duty cheio e 16383.
  // Logo 1 us vale 0,819 contagem.
  const uint32_t d0   = corpo::Servos::dutyDoAngulo(0);
  const uint32_t d90  = corpo::Servos::dutyDoAngulo(90);
  const uint32_t d180 = corpo::Servos::dutyDoAngulo(180);

  confereInt("0 grau  -> 500 us", (long)d0, (long)((500L * 16383L) / 20000L));
  confereInt("90 graus-> 1450 us", (long)d90, (long)((1450L * 16383L) / 20000L));
  confereInt("180 gr. -> 2400 us", (long)d180, (long)((2400L * 16383L) / 20000L));
  confere("o pulso cresce com o angulo", d0 < d90 && d90 < d180);

  // Um duty acima do maximo do timer nao gira o servo: ele apaga o
  // pulso. Melhor descobrir aqui do que com o braco no batente.
  confere("nunca passa do fundo de escala", d180 < (1u << PWM_SERVO_BITS));
}

// ---------------------------------------------------------------- 6
void secaoRampa() {
  Serial.println("\n[6] a rampa (hoje desligada)");

  confereInt("com passo grande chega direto", corpo::Motores::aproxima(0, 100, 200), 100);
  confereInt("nao passa do alvo", corpo::Motores::aproxima(95, 100, 10), 100);

  // PARTIDA DO REPOUSO - o caso que a rampa existe para proteger, e o
  // unico que estava quebrado. `atual` em zero era lido como "sinal
  // diferente de positivo", entao a rampa era pulada justamente na
  // partida. Achado com o autoteste rodando no C3 em 29/09/2026.
  confereInt("sobe em passos partindo do zero", corpo::Motores::aproxima(0, 100, 10), 10);
  confereInt("o mesmo para tras", corpo::Motores::aproxima(0, -100, 10), -10);
  confereInt("segundo passo", corpo::Motores::aproxima(10, 100, 10), 20);
  confereInt("parado continua parado", corpo::Motores::aproxima(0, 0, 10), 0);

  // A parte que e seguranca: descer e imediato. Rampa na frenagem
  // atrasaria uma parada de emergencia, e proteger a fonte nao vale
  // isso.
  confereInt("descida e imediata", corpo::Motores::aproxima(100, 0, 1), 0);
  confereInt("inversao de sentido e imediata", corpo::Motores::aproxima(80, -80, 1), -80);

  Serial.printf("  RAMPA_SUBIDA_MS = %d (%s)\n", RAMPA_SUBIDA_MS,
                RAMPA_SUBIDA_MS == 0 ? "comando vale na hora" : "rampa ativa");
}

// ---- O manche do PS4 virando duas rodas ------------------------------
void secaoManche() {
  Serial.println("\n[7] o manche do PS4 (a conta, sem controle)");
  int e = 0, d = 0;

  // O centro e o raio morto: um controle de verdade nunca devolve zero
  // exato, e sem raio morto o robo anda sozinho.
  confere("centro nao mexe", !corpo::ControlePS4::mistura(0, 0, e, d) && e == 0 && d == 0);
  confere("folga do manche nao mexe",
          !corpo::ControlePS4::mistura(PS4_RAIO_MORTO - 1, -(PS4_RAIO_MORTO - 1), e, d));

  corpo::ControlePS4::mistura(0, 127, e, d);
  confere("manche todo para a frente: 100 e 100", e == 100 && d == 100);
  corpo::ControlePS4::mistura(0, -127, e, d);
  confere("todo para tras: -100 e -100", e == -100 && d == -100);
  corpo::ControlePS4::mistura(127, 0, e, d);
  confere("todo para a direita gira no lugar: 100 e -100", e == 100 && d == -100);

  // Frente e direita juntos passariam de 100 numa roda: prende no
  // limite em vez de estourar a faixa do protocolo.
  corpo::ControlePS4::mistura(127, 127, e, d);
  confere("diagonal prende no limite: 100 e 0", e == 100 && d == 0);

  // Gatilho solto e o servo em repouso: conectar o controle nao pode
  // mexer servo nenhum.
  confere("gatilho solto: servo em repouso",
          corpo::ControlePS4::anguloDoGatilho(0) == SERVO_REPOUSO);
  const int fim = corpo::ControlePS4::anguloDoGatilho(255);
  confere("gatilho no fim: repouso + curso",
          fim == constrain(SERVO_REPOUSO + PS4_SERVO_CURSO, 0, 180));
  const int meio = corpo::ControlePS4::anguloDoGatilho(128);
  confere("gatilho no meio: entre os dois",
          (meio - SERVO_REPOUSO) * (fim - SERVO_REPOUSO) > 0 &&
              abs(meio - SERVO_REPOUSO) < abs(fim - SERVO_REPOUSO));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t ate = millis() + 2000;
  while (!Serial && millis() < ate) delay(10);

  Serial.println("\n=====================================================");
  Serial.println(" Feijao com Farinha - autoteste da logica");
  Serial.printf(" placa: %s, %lu MHz\n", ESP.getChipModel(), (unsigned long)ESP.getCpuFreqMHz());
  Serial.println("=====================================================");

  const uint32_t t0 = millis();
  secaoParser();
  secaoFailsafe();
  secaoLinha();
  secaoMontagem();
  secaoServo();
  secaoRampa();
  secaoManche();
  const uint32_t dt = millis() - t0;

  Serial.println("\n-----------------------------------------------------");
  Serial.printf(" %lu verificacoes, %lu passaram, %lu falharam, em %lu ms\n",
                (unsigned long)(passaram + falharam), (unsigned long)passaram,
                (unsigned long)falharam, (unsigned long)dt);
  Serial.println(falharam == 0 ? " TUDO PASSOU" : " HA FALHA - ler acima");
  Serial.println("-----------------------------------------------------");
}

void loop() {
  delay(1000);
}
