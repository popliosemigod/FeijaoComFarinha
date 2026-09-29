// =====================================================================
//  microfone.h - o microfone PDM embutido na placa Sense
//
//  16 kHz, 16 bits, mono. O buffer mora na PSRAM: 1 segundo de audio
//  custa 32 KB, e a RAM interna do S3 tem outras contas a pagar
//  (Wi-Fi, camera, pilhas de task). Com 8 MB de PSRAM, alguns segundos
//  de fala nao custam nada.
//
//  O stream para o servidor NAO esta aqui, e de proposito: ele depende
//  de qual servico de voz sera usado, que e a decisao que ainda nao
//  foi tomada. Este arquivo entrega amostras; `voz.h` decide para onde
//  elas vao.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

#include "config_cerebro.h"

namespace cerebro {

class Microfone {
public:
  bool begin() {
    i2s_.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
    pronto_ =
        i2s_.begin(I2S_MODE_PDM_RX, MIC_TAXA_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    return pronto_;
  }

  bool pronto() const { return pronto_; }

  // Le ate `quantas` amostras, sem bloquear alem do que ja esta no
  // buffer do periferico. Devolve quantas realmente vieram.
  size_t le(int16_t* destino, size_t quantas) {
    if (!pronto_) return 0;
    const size_t bytes = i2s_.readBytes((char*)destino, quantas * sizeof(int16_t));
    return bytes / sizeof(int16_t);
  }

  // Nivel medio do ultimo bloco, de 0 a 1. E o diagnostico mais barato
  // do microfone: com ele da para ver no monitor que a placa esta
  // ouvindo, sem gravar arquivo nenhum e sem servidor.
  static float nivel(const int16_t* amostras, size_t quantas) {
    if (quantas == 0) return 0.0f;
    uint64_t soma = 0;
    for (size_t i = 0; i < quantas; i++) soma += (uint64_t)abs(amostras[i]);
    return (float)(soma / quantas) / 32768.0f;
  }

private:
  I2SClass i2s_;
  bool pronto_ = false;
};

}  // namespace cerebro
