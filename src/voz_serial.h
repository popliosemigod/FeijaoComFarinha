// =====================================================================
//  voz_serial.h - a escuta: recorta frases e manda para o PC transcrever
//
//  E o caminho 1 do `voz.h` - Whisper rodando na maquina do
//  laboratorio -, pelo fio que ja existe. O Wi-Fi ainda nao tem
//  credencial; o USB do cerebro tem banda de sobra para audio.
//
//  A placa NAO transcreve. Ela faz so a metade que so ela pode fazer:
//  decidir onde uma frase comeca e onde acaba, guardar essas amostras
//  e despejar. Do outro lado, `scripts/ouve.py` roda o Whisper e
//  devolve o texto na tela.
//
//  POR QUE RECORTAR NA PLACA, e nao mandar audio o tempo todo: 16 kHz
//  em base64 sao 43 KB/s de texto atravessando o mesmo console que
//  carrega o log. Recortando, o console fica legivel enquanto ninguem
//  fala, e o Whisper recebe uma frase inteira em vez de pedacos de
//  tamanho fixo cortados no meio de uma palavra.
//
//  O RECORTE, com os numeros medidos na XIAO em 01/10/2026 (nivel sem
//  a componente continua): sala em silencio 0,0005 a 0,002; voz perto
//  da placa 0,01 a 0,055; voz a um metro 0,003 a 0,004.
//
//    comeca   2 blocos seguidos acima do limiar (um estalo e 1 bloco)
//    limiar   o maior entre VOZ_LIMIAR_MIN e 3x o ruido de fundo
//    guarda   tambem os 300 ms ANTES do comeco: a primeira consoante
//             de uma frase e fraca, e sem isso "para" vira "ara"
//    acaba    800 ms de silencio, ou VOZ_FRASE_MAX_S
//    descarta o que teve menos de 250 ms de voz - e batida, nao fala
//
//  O formato repete o da foto (`camera.h`): base64 entre marcadores,
//  porque o console e de texto e binario cru se perderia nele.
//
//     ---AUDIO-INICIO <amostras> <taxa>---
//     <base64 de PCM 16 bits, little-endian, mono>
//     ---AUDIO-FIM---
//
//  Cada escrita termina em fim de linha. E o que deixa o log de outra
//  task cair ENTRE duas linhas de base64, e nunca no meio de uma - o
//  defeito que a foto ainda tem.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "config_cerebro.h"
#include "voz.h"

namespace cerebro {

static const float VOZ_LIMIAR_MIN       = 0.003f;
static const float VOZ_LIMIAR_VEZES     = 3.0f;
static const uint32_t VOZ_FRASE_MAX_S   = 10;
static const uint32_t VOZ_PRE_MS        = 300;
static const uint32_t VOZ_FIM_MS        = 800;
static const uint32_t VOZ_MIN_MS        = 250;
static const uint8_t VOZ_BLOCOS_COMECAR = 2;

class VozPorSerial : public ServicoDeVoz {
public:
  bool begin() override {
    cap_ = VOZ_FRASE_MAX_S * MIC_TAXA_HZ;
    pre_ = (VOZ_PRE_MS * MIC_TAXA_HZ) / 1000;

    // A frase mora na PSRAM: 10 s sao 320 KB, e a RAM interna tem
    // dono (Wi-Fi, camera). Sem PSRAM a escuta fica desligada, e o
    // resto do firmware segue.
    frase_ = (int16_t*)ps_malloc(cap_ * sizeof(int16_t));
    anel_  = (int16_t*)malloc(pre_ * sizeof(int16_t));
    if (frase_ == nullptr || anel_ == nullptr) {
      Serial.println("[voz] sem memoria para a frase - escuta desligada");
      return false;
    }
    Serial.printf("[voz] escuta pronta: frases de ate %lu s. `o` liga, `q` desliga\n",
                  (unsigned long)VOZ_FRASE_MAX_S);
    return true;
  }

  // Liga ou desliga o despejo. Desligado, o microfone continua sendo
  // medido - so nao sai audio nenhum pelo console.
  void escuta(bool ligada) {
    ligada_ = ligada && frase_ != nullptr;
    estado_ = Estado::ESPERANDO;
    acima_  = 0;
  }

  void ouve(const int16_t* amostras, size_t quantas) override {
    if (quantas == 0) return;
    const float n = nivel(amostras, quantas);
    if (n > pico_) pico_ = n;
    blocos_++;
    if (!ligada_ || estado_ == Estado::PRONTA) return;

    const uint32_t ms = (uint32_t)((quantas * 1000UL) / MIC_TAXA_HZ);

    if (estado_ == Estado::ESPERANDO) {
      for (size_t i = 0; i < quantas; i++) {
        anel_[anel_pos_] = amostras[i];
        anel_pos_        = (anel_pos_ + 1) % pre_;
        if (anel_cheio_ < pre_) anel_cheio_++;
      }
      if (n > limiar()) {
        if (++acima_ >= VOZ_BLOCOS_COMECAR) comeca();
      } else {
        acima_ = 0;
        // O fundo SOBE devagar e DESCE depressa. Com a mesma pressa nos
        // dois sentidos, fala fraca - longe da placa - empurrava o
        // fundo para cima antes de passar do limiar, o limiar subia
        // junto, e a frase nunca comecava. Visto em 01/10/2026: de
        // tres frases a um metro, so a mais curta entrou.
        fundo_ += (n - fundo_) * (n > fundo_ ? 0.002f : 0.05f);
      }
      return;
    }

    // GRAVANDO
    const size_t cabe = cap_ - n_;
    const size_t leva = quantas < cabe ? quantas : cabe;
    memcpy(frase_ + n_, amostras, leva * sizeof(int16_t));
    n_ += leva;

    if (n > limiar() * 0.6f) {
      silencio_ms_ = 0;
      voz_ms_ += ms;
    } else {
      silencio_ms_ += ms;
    }

    if (silencio_ms_ >= VOZ_FIM_MS || n_ >= cap_) {
      if (voz_ms_ >= VOZ_MIN_MS) {
        estado_ = Estado::PRONTA;
      } else {
        estado_ = Estado::ESPERANDO;  // batida, tosse de um bloco: nao e frase
        acima_  = 0;
      }
    }
  }

  // Manda a frase pronta, se houver. Chamado pela MESMA task que chama
  // `ouve()`, logo depois dele: enquanto isto escreve, o microfone nao
  // e lido - e tudo bem, ninguem quer a frase seguinte colada nesta.
  void despacha(Print& saida) {
    if (estado_ != Estado::PRONTA) return;

    saida.printf("---AUDIO-INICIO %u %u---\n", (unsigned)n_, (unsigned)MIC_TAXA_HZ);

    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* bytes = (const uint8_t*)frase_;
    const size_t total   = n_ * sizeof(int16_t);

    // 57 bytes viram 76 caracteres: uma linha. Doze linhas por
    // escrita - poucas chamadas, e toda escrita acaba em '\n'.
    static char lote[12 * 77];
    size_t no_lote = 0;
    uint8_t linhas = 0;
    for (size_t i = 0; i < total; i += 57) {
      const size_t fim = (i + 57 < total) ? i + 57 : total;
      for (size_t k = i; k < fim; k += 3) {
        const size_t sobra = fim - k;
        uint32_t v         = (uint32_t)bytes[k] << 16;
        if (sobra > 1) v |= (uint32_t)bytes[k + 1] << 8;
        if (sobra > 2) v |= bytes[k + 2];
        lote[no_lote++] = T[(v >> 18) & 63];
        lote[no_lote++] = T[(v >> 12) & 63];
        lote[no_lote++] = sobra > 1 ? T[(v >> 6) & 63] : '=';
        lote[no_lote++] = sobra > 2 ? T[v & 63] : '=';
      }
      lote[no_lote++] = '\n';
      if (++linhas == 12) {
        saida.write((const uint8_t*)lote, no_lote);
        no_lote = 0;
        linhas  = 0;
      }
    }
    if (no_lote > 0) saida.write((const uint8_t*)lote, no_lote);
    saida.print("---AUDIO-FIM---\n");

    frases_++;
    estado_ = Estado::ESPERANDO;
    acima_  = 0;
  }

  bool pegaIntencao(Entendido&) override { return false; }

  void diz(const char* frase) override { Serial.printf("[voz] diria: \"%s\"\n", frase); }

  bool ligado() const override { return ligada_; }

  // Para o `?` do console.
  float picoEZera() {
    const float p = pico_;
    pico_         = 0.0f;
    return p;
  }
  uint32_t blocos() const { return blocos_; }
  uint32_t frases() const { return frases_; }
  float fundo() const { return fundo_; }

  // Nivel de um bloco, de 0 a 1, sem a componente continua - o offset
  // DC do microfone sozinho ja pareceria som.
  static float nivel(const int16_t* amostras, size_t quantas) {
    int32_t soma = 0;
    for (size_t i = 0; i < quantas; i++) soma += amostras[i];
    const int32_t media = soma / (int32_t)quantas;
    uint32_t desvio     = 0;
    for (size_t i = 0; i < quantas; i++) desvio += (uint32_t)abs(amostras[i] - media);
    return (float)(desvio / quantas) / 32768.0f;
  }

private:
  enum class Estado : uint8_t {
    ESPERANDO,
    GRAVANDO,
    PRONTA
  };

  float limiar() const {
    const float relativo = fundo_ * VOZ_LIMIAR_VEZES;
    return relativo > VOZ_LIMIAR_MIN ? relativo : VOZ_LIMIAR_MIN;
  }

  // A frase comeca com o que o anel guardou, na ordem em que chegou.
  void comeca() {
    const size_t inicio = (anel_cheio_ < pre_) ? 0 : anel_pos_;
    for (size_t i = 0; i < anel_cheio_; i++) frase_[i] = anel_[(inicio + i) % pre_];
    n_           = anel_cheio_;
    silencio_ms_ = 0;
    voz_ms_      = 0;
    anel_cheio_  = 0;
    anel_pos_    = 0;
    estado_      = Estado::GRAVANDO;
  }

  int16_t* frase_ = nullptr;
  int16_t* anel_  = nullptr;
  size_t cap_     = 0;
  size_t pre_     = 0;
  size_t n_       = 0;

  size_t anel_pos_   = 0;
  size_t anel_cheio_ = 0;

  Estado estado_        = Estado::ESPERANDO;
  bool ligada_          = false;
  uint8_t acima_        = 0;
  uint32_t silencio_ms_ = 0;
  uint32_t voz_ms_      = 0;
  float fundo_          = 0.001f;

  float pico_      = 0.0f;
  uint32_t blocos_ = 0;
  uint32_t frases_ = 0;
};

}  // namespace cerebro
