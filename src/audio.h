// =====================================================================
//  audio.h - a voz sai por aqui (MAX98357A, I2S de saida)
//
//  PCM 16 bits mono a 16 kHz e a prioridade, e nao e economia: e a
//  taxa em que a fala cabe inteira (a voz humana vive abaixo de 4 kHz)
//  e em que 1 segundo custa 32 KB. Com MP3 a decodificacao disputaria
//  CPU com a camera; com WAV a placa so empurra bytes.
//
//  O amplificador e o microfone usam PERIFERICOS I2S DIFERENTES, e por
//  isso podem ter taxas diferentes e funcionar ao mesmo tempo. Se
//  dividissem o periferico, gravar enquanto fala seria impossivel - e
//  um robo que precisa ouvir "para!" no meio da propria frase precisa
//  exatamente disso.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

#include "config_cerebro.h"

namespace cerebro {

class Audio {
public:
  bool begin() {
    i2s_.setPins(PIN_AUDIO_BCLK, PIN_AUDIO_LRC, PIN_AUDIO_DIN, -1, -1);
    pronto_ = i2s_.begin(I2S_MODE_STD, AUDIO_TAXA_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    return pronto_;
  }

  bool pronto() const { return pronto_; }

  // Empurra PCM 16 bits mono ja pronto. Bloqueia enquanto o buffer do
  // I2S nao aceita mais - o que e o comportamento certo para quem
  // chama de uma task de audio, e o motivo de nao se chamar isto do
  // laco principal.
  size_t toca(const int16_t* amostras, size_t quantas) {
    if (!pronto_) return 0;
    return i2s_.write((uint8_t*)amostras, quantas * sizeof(int16_t));
  }

  // Um bipe. Existe para responder a pergunta mais barata da bancada:
  // "o amplificador esta ligado e o fio esta certo?" - sem depender de
  // rede, cartao ou serviço de voz nenhum.
  void bipe(uint16_t hz = 880, uint16_t ms = 150, float volume = 0.25f) {
    if (!pronto_) return;
    const size_t total = (size_t)((uint32_t)AUDIO_TAXA_HZ * ms / 1000);
    int16_t bloco[128];
    float fase       = 0.0f;
    const float pass = 2.0f * PI * (float)hz / (float)AUDIO_TAXA_HZ;
    size_t feitas    = 0;
    while (feitas < total) {
      const size_t n = min(sizeof(bloco) / sizeof(bloco[0]), total - feitas);
      for (size_t i = 0; i < n; i++) {
        bloco[i] = (int16_t)(sinf(fase) * 32000.0f * volume);
        fase += pass;
        if (fase > 2.0f * PI) fase -= 2.0f * PI;
      }
      toca(bloco, n);
      feitas += n;
    }
    silencio(20);
  }

  // Alguns milissegundos de zero depois de falar. Sem isto, o ultimo
  // valor fica preso na saida e o MAX98357A chia baixinho ate o
  // proximo som.
  void silencio(uint16_t ms) {
    if (!pronto_) return;
    int16_t zeros[64]  = {0};
    const size_t total = (size_t)((uint32_t)AUDIO_TAXA_HZ * ms / 1000);
    for (size_t f = 0; f < total; f += 64) toca(zeros, min((size_t)64, total - f));
  }

private:
  I2SClass i2s_;
  bool pronto_ = false;
};

}  // namespace cerebro
