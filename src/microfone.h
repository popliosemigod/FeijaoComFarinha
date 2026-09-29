// =====================================================================
//  microfone.h - a escuta, nas duas placas
//
//  Duas origens possiveis, e `le()` devolve a mesma coisa nas duas:
//  amostras de 16 bits, mono, a 16 kHz.
//
//    XIAO S3 Sense   microfone PDM embutido            (MIC_PDM 1)
//    ESP32-CAM       INMP441 externo, I2S padrao       (MIC_PDM 0)
//
//  Quem chama nao sabe qual e, e nao deve saber. Foi para isso que a
//  conversao ficou aqui dentro.
//
//  O stream para o servidor NAO esta neste arquivo, e de proposito:
//  ele depende de qual servico de voz sera usado, que e a decisao que
//  ainda nao foi tomada. Aqui entrega-se amostra; `voz.h` decide para
//  onde ela vai.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

#include "config_cerebro.h"

namespace cerebro {

class Microfone {
public:
  bool begin() {
#ifdef MIC_PORTA_I2S
    // Controlador fixo, e nao automatico. A razao esta no
    // `config_cerebro_cam.h`: com a camera segurando o I2S0 por um
    // driver diferente, a alocacao automatica nao ve o conflito, tenta
    // o I2S0 assim mesmo e falha.
    i2s_.setPort(MIC_PORTA_I2S);
#endif

#if MIC_PDM
    i2s_.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
    pronto_ =
        i2s_.begin(I2S_MODE_PDM_RX, MIC_TAXA_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
#else
    // Sem DOUT: so entrada. O -1 no lugar do dado de saida e o que
    // deixa o pino do amplificador livre enquanto ele nao existe.
    i2s_.setPins(PIN_MIC_BCLK, PIN_MIC_WS, -1, PIN_MIC_DATA, -1);

    // 32 BITS, E NAO 16, e isto nao e desperdicio.
    //
    // O INMP441 entrega 24 bits uteis dentro de um slot de 32, com um
    // atraso de um clock. Pedir slot de 16 nao "trunca educadamente":
    // o microfone precisa de pelo menos 24 SCK por canal, e com menos
    // ele devolve dado deslocado ou zero - que no monitor aparece como
    // "o microfone nao funciona", mandando procurar defeito na solda.
    //
    // Entao le-se 32 e reduz-se para 16 aqui dentro.
    pronto_ = i2s_.begin(I2S_MODE_STD, MIC_TAXA_HZ, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
#endif
    return pronto_;
  }

  bool pronto() const { return pronto_; }

  // Le ate `quantas` amostras. Devolve quantas realmente vieram.
  size_t le(int16_t* destino, size_t quantas) {
    if (!pronto_ || quantas == 0) return 0;

#if MIC_PDM
    const size_t bytes = i2s_.readBytes((char*)destino, quantas * sizeof(int16_t));
    return bytes / sizeof(int16_t);
#else
    if (quantas > CRU_MAX) quantas = CRU_MAX;
    const size_t bytes = i2s_.readBytes((char*)cru_, quantas * sizeof(int32_t));
    const size_t n     = bytes / sizeof(int32_t);
    for (size_t i = 0; i < n; i++) {
      // Os 24 bits uteis ficam no topo da palavra de 32. Deslocar 16
      // pega os 16 mais significativos deles, que e a amostra que o
      // resto do firmware espera.
      destino[i] = (int16_t)(cru_[i] >> 16);
    }
    return n;
#endif
  }

  // Nivel medio do ultimo bloco, de 0 a 1. E o diagnostico mais barato
  // que existe: com ele da para ver no monitor que a placa esta
  // ouvindo, sem gravar arquivo nenhum e sem servidor nenhum.
  static float nivel(const int16_t* amostras, size_t quantas) {
    if (quantas == 0) return 0.0f;
    uint64_t soma = 0;
    for (size_t i = 0; i < quantas; i++) soma += (uint64_t)abs(amostras[i]);
    return (float)(soma / quantas) / 32768.0f;
  }

private:
  I2SClass i2s_;
  bool pronto_ = false;
#if !MIC_PDM
  static const size_t CRU_MAX = 256;
  int32_t cru_[CRU_MAX]       = {0};
#endif
};

}  // namespace cerebro
