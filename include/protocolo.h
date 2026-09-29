// =====================================================================
//  protocolo.h - o combinado entre o cerebro e o corpo
//
//  ESTE ARQUIVO E COMPILADO NAS DUAS PLACAS. E de proposito: um
//  protocolo escrito duas vezes e um protocolo que diverge. Quando o
//  timeout do failsafe mudar, ele muda aqui e as duas pontas mudam
//  juntas - nao existe versao em que uma placa acredita em 1000 ms e a
//  outra em 1500.
//
//  Forma: texto ASCII, um comando por linha, terminado em '\n'. O '\r'
//  e ignorado, porque terminal serial de Windows manda CRLF e ninguem
//  quer descobrir isso as duas da manha.
//
//  Por que texto e nao binario: as duas pontas precisam ser depuraveis
//  com um terminal serial e um teclado. Num robo que anda, poder abrir
//  o monitor e digitar `M 40 40` sem nenhum software do lado do PC vale
//  mais do que os bytes economizados.
//
//     Comando          Efeito
//     ---------------  -------------------------------------------
//     M <esq> <dir>    velocidade dos motores, -100 a 100
//     S <n> <angulo>   servo n (1 ou 2) no angulo 0-180
//     STOP             para os dois motores
//     EN <0|1>         desliga/liga as pontes (0 tambem zera o PWM)
//     PING             responde PONG
//
//     Resposta         Quando
//     ---------------  -------------------------------------------
//     OK               comando aceito
//     ERR <motivo>     comando recusado, com o motivo em uma palavra
//     PONG             resposta ao PING
// =====================================================================
#pragma once

#include <stdint.h>
#include <stdio.h>

namespace protocolo {

// ---- Enlace ---------------------------------------------------------
static const uint32_t BAUD = 115200;

// Tamanho maximo de uma linha. A maior linha valida e `M -100 -100`,
// com 12 caracteres; 64 da folga de sobra para o parser e mantem o
// buffer pequeno o bastante para viver na pilha de uma task.
static const uint8_t LINHA_MAX = 64;

// ---- O failsafe -----------------------------------------------------
//
//  ISTO E SEGURANCA, E E O NUMERO MAIS IMPORTANTE DESTE ARQUIVO.
//
//  Um robo com motor de tracao que perde o cerebro nao para sozinho: o
//  ultimo `M` continua valendo e ele sai andando ate bater em alguma
//  coisa ou cair da mesa. Por isso quem para nao e o cerebro - e o
//  corpo, contando o tempo desde o ultimo comando de movimento.
//
//  A conta: o cerebro reenvia a cada HEARTBEAT_MS; o corpo desiste em
//  SILENCIO_MS. A folga de 4x existe porque o cerebro tem Wi-Fi, camera
//  e audio disputando dois nucleos, e um atraso de 200 ms ali e normal.
//  Folga menor faria o robo tremer - parar e voltar a andar - por
//  jitter de escalonamento, e tremer parece defeito eletrico.
static const uint32_t SILENCIO_MS  = 1000;  // corpo para sozinho
static const uint32_t HEARTBEAT_MS = 250;   // cerebro reenvia

// Comando de servo NAO conta como heartbeat. Um robo que anda em linha
// reta mexendo so a cabeca continua sendo um robo que anda: se o enlace
// de movimento morreu, ele tem que parar mesmo que os servos ainda
// estejam obedecendo.

// ---- Faixas ---------------------------------------------------------
static const int VEL_MIN    = -100;
static const int VEL_MAX    = 100;
static const int ANG_MIN    = 0;
static const int ANG_MAX    = 180;
static const uint8_t SERVOS = 2;

// ---- Respostas ------------------------------------------------------
static const char RESP_OK[]   = "OK";
static const char RESP_PONG[] = "PONG";

// Motivos de recusa. O motivo e uma palavra so, sem espaco: o cerebro
// compara com strcmp e o humano le no monitor sem precisar de tabela.
static const char ERR_COMANDO[] = "ERR comando-desconhecido";
static const char ERR_ARGS[]    = "ERR argumentos";
static const char ERR_FAIXA[]   = "ERR fora-de-faixa";
static const char ERR_LONGA[]   = "ERR linha-longa";

// ---- O que uma linha vira depois de lida ----------------------------
enum class Tipo : uint8_t {
  NADA,      // linha vazia - nao e erro, so nao e comando
  MOTOR,     // M <esq> <dir>
  SERVO,     // S <n> <angulo>
  PARAR,     // STOP
  HABILITA,  // EN <0|1>
  PING,
  ERRO
};

struct Comando {
  Tipo tipo        = Tipo::NADA;
  int a            = 0;  // M: esquerdo | S: numero do servo | EN: 0 ou 1
  int b            = 0;  // M: direito  | S: angulo
  const char* erro = nullptr;

  // Movimento e o que alimenta o failsafe. PING entra porque e o
  // "estou vivo" explicito do cerebro, e EN porque desligar a ponte e
  // uma decisao ativa - nenhum dos dois pode parecer silencio.
  bool alimentaFailsafe() const {
    return tipo == Tipo::MOTOR || tipo == Tipo::PARAR || tipo == Tipo::HABILITA ||
           tipo == Tipo::PING;
  }
};

// ---- Leitura de uma linha -------------------------------------------
//
//  Funcao pura, sem hardware: e ela que o autoteste exercita na placa
//  nua, sem motor, sem servo e sem cabo entre as placas. Parser testado
//  so com o robo montado e parser testado tarde demais.
//
//  Ela NAO modifica a linha recebida - nada de strtok. Parser que
//  destroi a propria entrada impede que a mesma linha seja relida para
//  o log, que e exatamente o que se quer quando ela foi recusada.
inline Comando interpreta(const char* linha) {
  Comando c;
  if (linha == nullptr) return c;

  // Pula espaco a esquerda; o '\r' do terminal de Windows some aqui.
  const char* p = linha;
  while (*p == ' ' || *p == '\t' || *p == '\r') p++;
  if (*p == '\0') return c;  // Tipo::NADA

  // Le um inteiro com sinal. Devolve false se nao houver numero
  // nenhum, o que e diferente de ter lido o numero zero.
  auto inteiro = [](const char*& q, int& destino) -> bool {
    while (*q == ' ' || *q == '\t') q++;
    bool negativo = false;
    if (*q == '-' || *q == '+') {
      negativo = (*q == '-');
      q++;
    }
    if (*q < '0' || *q > '9') return false;
    long v = 0;
    while (*q >= '0' && *q <= '9') {
      v = v * 10 + (*q - '0');
      if (v > 100000) v = 100000;  // trava de estouro
      q++;
    }
    destino = (int)(negativo ? -v : v);
    return true;
  };

  const char a = *p;
  const char b = *(p + 1);

  // M <esq> <dir>
  if ((a == 'M' || a == 'm') && (b == ' ' || b == '\t')) {
    p++;
    if (!inteiro(p, c.a) || !inteiro(p, c.b)) {
      c.tipo = Tipo::ERRO;
      c.erro = ERR_ARGS;
      return c;
    }
    if (c.a < VEL_MIN || c.a > VEL_MAX || c.b < VEL_MIN || c.b > VEL_MAX) {
      c.tipo = Tipo::ERRO;
      c.erro = ERR_FAIXA;
      return c;
    }
    c.tipo = Tipo::MOTOR;
    return c;
  }

  // S <n> <angulo>
  if ((a == 'S' || a == 's') && (b == ' ' || b == '\t')) {
    p++;
    if (!inteiro(p, c.a) || !inteiro(p, c.b)) {
      c.tipo = Tipo::ERRO;
      c.erro = ERR_ARGS;
      return c;
    }
    if (c.a < 1 || c.a > (int)SERVOS || c.b < ANG_MIN || c.b > ANG_MAX) {
      c.tipo = Tipo::ERRO;
      c.erro = ERR_FAIXA;
      return c;
    }
    c.tipo = Tipo::SERVO;
    return c;
  }

  // EN <0|1>
  if ((a == 'E' || a == 'e') && (b == 'N' || b == 'n')) {
    p += 2;
    if (!inteiro(p, c.a)) {
      c.tipo = Tipo::ERRO;
      c.erro = ERR_ARGS;
      return c;
    }
    if (c.a != 0 && c.a != 1) {
      c.tipo = Tipo::ERRO;
      c.erro = ERR_FAIXA;
      return c;
    }
    c.tipo = Tipo::HABILITA;
    return c;
  }

  // STOP e PING nao levam argumento. Comparacao propria, e nao
  // strncmp, para aceitar tambem a forma em minuscula digitada a mao
  // no monitor serial.
  auto palavra = [](const char* q, const char* alvo) -> bool {
    while (*alvo) {
      char c1 = *q;
      if (c1 >= 'a' && c1 <= 'z') c1 = (char)(c1 - 32);
      if (c1 != *alvo) return false;
      q++;
      alvo++;
    }
    // Depois da palavra so pode vir fim de linha ou espaco.
    return *q == '\0' || *q == ' ' || *q == '\t' || *q == '\r';
  };

  if (palavra(p, "STOP")) {
    c.tipo = Tipo::PARAR;
    return c;
  }
  if (palavra(p, "PING")) {
    c.tipo = Tipo::PING;
    return c;
  }

  c.tipo = Tipo::ERRO;
  c.erro = ERR_COMANDO;
  return c;
}

// ---- Montagem de uma linha, do lado do cerebro ----------------------
// Sem String: o cerebro monta comando dentro de uma task periodica, e
// alocacao dinamica em regime e justamente o que fragmenta a heap
// depois de horas ligada - a falha que so aparece no ensaio longo.
inline uint8_t montaMotor(char* destino, uint8_t tamanho, int esq, int dir) {
  if (esq < VEL_MIN) esq = VEL_MIN;
  if (esq > VEL_MAX) esq = VEL_MAX;
  if (dir < VEL_MIN) dir = VEL_MIN;
  if (dir > VEL_MAX) dir = VEL_MAX;
  return (uint8_t)snprintf(destino, tamanho, "M %d %d\n", esq, dir);
}

inline uint8_t montaServo(char* destino, uint8_t tamanho, int n, int angulo) {
  if (angulo < ANG_MIN) angulo = ANG_MIN;
  if (angulo > ANG_MAX) angulo = ANG_MAX;
  return (uint8_t)snprintf(destino, tamanho, "S %d %d\n", n, angulo);
}

}  // namespace protocolo
