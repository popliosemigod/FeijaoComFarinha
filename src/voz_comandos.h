// =====================================================================
//  voz_comandos.h - o que foi dito vira ordem
//
//  O microfone e a placa recortam a frase, o PC a transcreve
//  (`scripts/ouve.py`, Whisper) e devolve o texto pelo mesmo USB. Quem
//  decide o que fazer com ele e o cerebro, aqui. O vocabulario e o
//  minimo que se dirige sem manual:
//
//    frente               anda para a frente
//    tras / re            anda para tras
//    esquerda / direita   vira no lugar
//    pare / parar / stop  para
//
//  "PARA" E A ARMADILHA. Em portugues "para" e parar e e preposicao:
//  "siga para frente" contem "para". Entao: "pare", "parar", "stop" e
//  "chega" param sempre, mesmo com direcao na frase ("pare de ir para
//  frente"); "para" sozinho so para quando nenhuma direcao foi dita.
//  Com duas direcoes, vale a que veio primeiro.
//
//  COMANDO E CURTO. Frase de mais de VOZ_PALAVRAS_MAX palavras so serve
//  para parar. Na bancada, em 08/10/2026, um "Pare." fraco virou "para
//  tras mais um resto so, repare" - o Whisper inventando em cima de
//  ruido -, e sem este corte o robo teria andado para tras. Parar
//  continua valendo em qualquer frase: parar por engano e seguro.
//
//  A conta e pura - sem hardware, sem rede - e o autoteste a exercita.
// =====================================================================
#pragma once

#include <Arduino.h>

#include <string.h>

namespace cerebro {

static const uint8_t VOZ_PALAVRAS_MAX = 6;

enum class Ordem : uint8_t {
  NADA,
  FRENTE,
  TRAS,
  ESQUERDA,
  DIREITA,
  PARA
};

inline const char* nomeDaOrdem(Ordem o) {
  switch (o) {
    case Ordem::FRENTE: return "frente";
    case Ordem::TRAS: return "tras";
    case Ordem::ESQUERDA: return "esquerda";
    case Ordem::DIREITA: return "direita";
    case Ordem::PARA: return "para";
    default: return "nada";
  }
}

namespace voz_detalhe {

// Minusculas, sem acento, e tudo que nao e letra vira espaco - com um
// espaco nas duas pontas, para achar palavra inteira com " frente ".
// Os acentos chegam em UTF-8 (0xC3 seguido de um byte): o Whisper
// escreve "trás", "ré", "à direita".
inline void normaliza(const char* texto, char* saida, size_t cabe) {
  size_t n = 0;
  if (cabe < 3) return;
  saida[n++] = ' ';
  for (const unsigned char* p = (const unsigned char*)texto; *p != '\0' && n < cabe - 2; p++) {
    unsigned char c = *p;
    if (c == 0xC3 && p[1] != '\0') {
      const unsigned char b = *++p | 0x20;  // maiuscula acentuada vira minuscula
      if (b >= 0xA0 && b <= 0xA5) {
        c = 'a';
      } else if (b == 0xA7) {
        c = 'c';
      } else if (b >= 0xA8 && b <= 0xAB) {
        c = 'e';
      } else if (b >= 0xAC && b <= 0xAF) {
        c = 'i';
      } else if (b >= 0xB2 && b <= 0xB6) {
        c = 'o';
      } else if (b >= 0xB9 && b <= 0xBC) {
        c = 'u';
      } else {
        c = ' ';
      }
    } else if (c >= 'A' && c <= 'Z') {
      c = (unsigned char)(c + 32);
    } else if (c < 'a' || c > 'z') {
      c = ' ';
    }
    saida[n++] = (char)c;
  }
  saida[n++] = ' ';
  saida[n]   = '\0';
}

// Quantas palavras a frase normalizada tem.
inline uint8_t palavras(const char* frase) {
  uint8_t n = 0;
  for (const char* p = frase; *p != '\0'; p++) {
    if (*p != ' ' && (p == frase || p[-1] == ' ') && n < 255) n++;
  }
  return n;
}

// Onde a palavra inteira aparece primeiro, ou -1.
inline int onde(const char* frase, const char* palavra) {
  char alvo[16];
  snprintf(alvo, sizeof(alvo), " %s ", palavra);
  const char* p = strstr(frase, alvo);
  return p == nullptr ? -1 : (int)(p - frase);
}

}  // namespace voz_detalhe

inline Ordem entende(const char* texto) {
  char f[128];
  voz_detalhe::normaliza(texto, f, sizeof(f));

  static const char* const PARA_SEMPRE[] = {"pare", "parar", "stop", "chega"};
  for (const char* p : PARA_SEMPRE) {
    if (voz_detalhe::onde(f, p) >= 0) return Ordem::PARA;
  }
  if (voz_detalhe::palavras(f) > VOZ_PALAVRAS_MAX) return Ordem::NADA;

  struct Palavra {
    const char* texto;
    Ordem ordem;
  };
  static const Palavra DIRECOES[] = {
      {"frente", Ordem::FRENTE}, {"tras", Ordem::TRAS},         {"atras", Ordem::TRAS},
      {"re", Ordem::TRAS},       {"esquerda", Ordem::ESQUERDA}, {"direita", Ordem::DIREITA},
  };
  Ordem melhor  = Ordem::NADA;
  int mais_cedo = -1;
  for (const Palavra& d : DIRECOES) {
    const int i = voz_detalhe::onde(f, d.texto);
    if (i >= 0 && (mais_cedo < 0 || i < mais_cedo)) {
      mais_cedo = i;
      melhor    = d.ordem;
    }
  }
  if (melhor != Ordem::NADA) return melhor;

  return voz_detalhe::onde(f, "para") >= 0 ? Ordem::PARA : Ordem::NADA;
}

// As ultimas frases ouvidas, para a pagina mostrar. So o loop() do
// cerebro escreve e le, entao nao precisa de trava.
class Ouvidos {
public:
  static constexpr uint8_t GUARDADOS = 4;

  struct Ouvido {
    char texto[64] = {0};
    Ordem ordem    = Ordem::NADA;
  };

  void anota(const char* texto, Ordem o) {
    proximo_ = (uint8_t)((proximo_ + 1) % GUARDADOS);
    char* t  = lista_[proximo_].texto;
    strlcpy(t, texto, sizeof(lista_[proximo_].texto));
    // Cortada no meio de um acento, a frase mostraria lixo na pagina: o
    // primeiro byte de um caractere UTF-8 solto no fim sai junto.
    const size_t n = strlen(t);
    if (n > 0 && (unsigned char)t[n - 1] >= 0xC0) t[n - 1] = '\0';
    lista_[proximo_].ordem = o;
    if (quantos_ < GUARDADOS) quantos_++;
    contador_++;
  }

  uint8_t quantos() const { return quantos_; }
  uint32_t contador() const { return contador_; }  // muda a cada frase
  // 0 e a mais recente.
  const Ouvido& ouvido(uint8_t i) const { return lista_[(proximo_ + GUARDADOS - i) % GUARDADOS]; }

private:
  Ouvido lista_[GUARDADOS];
  uint8_t proximo_   = 0;
  uint8_t quantos_   = 0;
  uint32_t contador_ = 0;
};

}  // namespace cerebro
