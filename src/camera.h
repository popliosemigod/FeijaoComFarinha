// =====================================================================
//  camera.h - a camera da placa Sense: os olhos do Jaspy
//
//  O Feijao com Farinha e o Jaspy com rodas, e esta camera e a visao
//  dele. Ela tira a foto, guarda as ultimas GUARDADAS na PSRAM e as
//  entrega por tres caminhos: a pagina do robo, para uma olhada rapida;
//  a rede (`/foto.jpg`, `/fotos` - ver web_cerebro.h), para o Jaspy; e
//  o USB em base64, que `scripts/fotos.py` salva no PC.
//
//  QVGA e JPEG por padrao. Nao e economia de pixel: em resolucao alta
//  o buffer some da PSRAM que o audio tambem quer, e a primeira coisa
//  que se quer saber e se a camera responde, nao se ela e bonita.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <esp_camera.h>

#include "config_cerebro.h"

namespace cerebro {

class Camera {
public:
  bool begin(framesize_t tamanho = FRAMESIZE_QVGA) {
    camera_config_t c = {};
    c.ledc_channel    = LEDC_CHANNEL_0;
    c.ledc_timer      = LEDC_TIMER_0;
    c.pin_d0          = PIN_CAM_Y2;
    c.pin_d1          = PIN_CAM_Y3;
    c.pin_d2          = PIN_CAM_Y4;
    c.pin_d3          = PIN_CAM_Y5;
    c.pin_d4          = PIN_CAM_Y6;
    c.pin_d5          = PIN_CAM_Y7;
    c.pin_d6          = PIN_CAM_Y8;
    c.pin_d7          = PIN_CAM_Y9;
    c.pin_xclk        = PIN_CAM_XCLK;
    c.pin_pclk        = PIN_CAM_PCLK;
    c.pin_vsync       = PIN_CAM_VSYNC;
    c.pin_href        = PIN_CAM_HREF;
    c.pin_sccb_sda    = PIN_CAM_SIOD;
    c.pin_sccb_scl    = PIN_CAM_SIOC;
    c.pin_pwdn        = PIN_CAM_PWDN;
    c.pin_reset       = PIN_CAM_RESET;
    c.xclk_freq_hz    = 20000000;
    c.pixel_format    = PIXFORMAT_JPEG;
    c.frame_size      = tamanho;
    c.jpeg_quality    = 12;
    c.fb_count        = 2;
    c.fb_location     = CAMERA_FB_IN_PSRAM;
    c.grab_mode       = CAMERA_GRAB_LATEST;

    // Sem PSRAM a camera ate inicializa, mas so com um buffer pequeno.
    // Cair para configuracao menor e melhor que falhar: o robo perde
    // resolucao, nao perde a camera.
    if (!psramFound()) {
      c.frame_size  = FRAMESIZE_QQVGA;
      c.fb_count    = 1;
      c.fb_location = CAMERA_FB_IN_DRAM;
    }

    const esp_err_t r = esp_camera_init(&c);
    pronto_           = (r == ESP_OK);
    if (!pronto_) erro_ = r;

    // ---- Ajuste para pouca luz ------------------------------------
    //
    // Um robo de sala vive em luz ruim: sombra de movel, corredor, e
    // o fim da noite. O padrao do OV2640 e conservador e entrega
    // quadro quase preto nessas condicoes - a primeira foto tirada
    // aqui saiu assim.
    //
    // O teto de ganho e o que muda isso de verdade. Ele nao clareia a
    // imagem sozinho: ele PERMITE que o controle automatico clareie,
    // ate 128x em vez dos 16x de fabrica. O custo e ruido, e num
    // quadro que ninguem ia enxergar o ruido e o menor problema.
    if (pronto_) {
      sensor_t* s = esp_camera_sensor_get();
      if (s != nullptr) {
        // A MESMA FUNCAO, OUTRA UNIDADE, e isto escureceu a XIAO por
        // uma semana. No OV2640 o argumento e um enum (6 = 128x). No
        // OV3660 o driver grava o numero CRU no teto de ganho, que conta
        // em dezesseis avos: o 6 do enum virava teto de 0,375x - menos
        // que ganho unitario - e toda foto saia com brilho medio 24 de
        // 255, de dia ou de noite. 0x200 sao 32x nessa unidade.
        // Achado em 05/10/2026, lendo o driver depois de quatro
        // tentativas de clarear a foto pelo lado errado.
        if (s->id.PID == OV3660_PID) {
          s->set_gainceiling(s, (gainceiling_t)0x200);
        } else {
          s->set_gainceiling(s, GAINCEILING_128X);
        }
        s->set_brightness(s, 1);
        s->set_gain_ctrl(s, 1);      // ganho automatico ligado
        s->set_exposure_ctrl(s, 1);  // exposicao automatica ligada
        s->set_aec2(s, 1);           // o algoritmo melhor dos dois
      }
    }
    return pronto_;
  }

  bool pronto() const { return pronto_; }
  esp_err_t erro() const { return erro_; }

  // Qual sensor a placa trouxe (0x26 = OV2640, 0x3660 = OV3660). A
  // Seeed vende a Sense com os dois, e os comentarios deste arquivo
  // falam do OV2640 porque foi com ele, na ESP32-CAM, que os ajustes
  // de pouca luz foram medidos.
  uint16_t sensor() const {
    sensor_t* s = pronto_ ? esp_camera_sensor_get() : nullptr;
    return s != nullptr ? s->id.PID : 0;
  }

  // Tira um quadro, guarda (ver captura()) e despeja em base64 pela
  // serial, entre marcadores.
  //
  // Existe porque esta placa NAO tem cartao SD - e enquanto nao houver
  // Wi-Fi configurado, este e o unico caminho para uma imagem sair
  // daqui e virar arquivo. O cabo de gravacao serve de cabo de dados.
  //
  // Base64 custa um terco a mais que o binario e atravessa terminal
  // sem que nenhum byte seja interpretado como controle - num link que
  // tambem carrega log em texto, binario cru se perderia.
  //
  // Descarta alguns quadros antes: o OV2640 sobe com a exposicao e o
  // balanco de branco ainda convergindo, e as primeiras imagens saem
  // esverdeadas ou pretas. "A primeira foto" deve ser a primeira
  // FOTO, nao o primeiro quadro.
  //
  // `descartar` em 12, e nao em 2 ou 3: o controle automatico do
  // OV2640 converge ao longo de varios quadros, e em luz fraca demora
  // mais. Com poucos descartes a imagem sai escura mesmo com o ganho
  // liberado - foi o que aconteceu na primeira tentativa.
  bool despeja(Print& saida, uint8_t descartar = 12) {
    if (!captura(descartar)) return false;
    despejaGuardada(saida);
    return true;
  }

  // A ultima foto de captura(), em base64, no mesmo formato de cima.
  // E o que sai quando o X do controle pede foto: a mesma imagem vai
  // para a pagina e, por aqui, para o PC (`scripts/fotos.py`).
  void despejaGuardada(Print& saida) const {
    const Foto* f = ultima();
    if (f == nullptr) return;
    const uint8_t* dados = f->buf;
    const size_t n       = f->tamanho;
    saida.printf("---FOTO-INICIO %u %u %u---\n", (unsigned)n, (unsigned)f->largura,
                 (unsigned)f->altura);

    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i             = 0;
    uint16_t na_linha    = 0;
    while (i + 2 < n) {
      const uint32_t v = ((uint32_t)dados[i] << 16) | ((uint32_t)dados[i + 1] << 8) | dados[i + 2];
      saida.write(T[(v >> 18) & 63]);
      saida.write(T[(v >> 12) & 63]);
      saida.write(T[(v >> 6) & 63]);
      saida.write(T[v & 63]);
      i += 3;
      if ((na_linha += 4) >= 76) {
        saida.write('\n');
        na_linha = 0;
      }
    }
    if (i < n) {  // o resto: um ou dois bytes
      const size_t sobra = n - i;
      uint32_t v         = (uint32_t)dados[i] << 16;
      if (sobra == 2) v |= (uint32_t)dados[i + 1] << 8;
      saida.write(T[(v >> 18) & 63]);
      saida.write(T[(v >> 12) & 63]);
      saida.write(sobra == 2 ? T[(v >> 6) & 63] : '=');
      saida.write('=');
    }
    saida.write('\n');
    saida.println("---FOTO-FIM---");
  }

  // Tira um quadro e DEVOLVE o buffer na mesma chamada, entregando so
  // o tamanho. Quadro que fica preso trava a camera depois de
  // `fb_count` capturas - a falha classica, que aparece como "a camera
  // funcionou duas vezes e parou".
  size_t mede() {
    if (!pronto_) return 0;
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) return 0;
    const size_t n = fb->len;
    esp_camera_fb_return(fb);
    return n;
  }

  // Uma foto guardada. `numero` cresce a cada captura e nao se repete
  // no mesmo boot: e por ele que a pagina e o Jaspy pedem uma foto.
  struct Foto {
    uint8_t* buf     = nullptr;
    size_t tamanho   = 0;
    uint16_t largura = 0;
    uint16_t altura  = 0;
    uint32_t numero  = 0;
  };

  // Quantas ficam na memoria. QVGA em JPEG da 8 a 20 KB: oito sao uns
  // 150 KB de uma PSRAM de 8 MB. A nona apaga a primeira.
  static constexpr uint8_t GUARDADAS = 8;

  // Tira um quadro (mesmo descarte de despeja(), pelo mesmo motivo) e
  // guarda uma COPIA propria, para o WebCerebro servir depois. Existe
  // porque o buffer do driver tem que voltar para a fila logo apos a
  // captura - sem copia, nao sobraria nada para atender um pedido HTTP
  // que chega alguns milissegundos depois.
  //
  // As ultimas GUARDADAS ficam: sao os olhos do Jaspy, e a pagina as
  // mostra para uma olhada rapida. Tudo isto roda no loop() do cerebro
  // (console, enlace e pagina), entao ninguem le uma foto enquanto
  // outra e gravada.
  bool captura(uint8_t descartar = 12) {
    if (!pronto_) return false;

    for (uint8_t i = 0; i < descartar; i++) {
      camera_fb_t* lixo = esp_camera_fb_get();
      if (lixo) esp_camera_fb_return(lixo);
      delay(120);
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) return false;

    uint8_t* copia = (uint8_t*)(psramFound() ? ps_malloc(fb->len) : malloc(fb->len));
    if (copia == nullptr) {
      esp_camera_fb_return(fb);
      return false;
    }
    memcpy(copia, fb->buf, fb->len);

    Foto& f = fotos_[proxima_];  // a mais antiga, ou uma vaga
    free(f.buf);
    f.buf     = copia;
    f.tamanho = fb->len;
    f.largura = fb->width;
    f.altura  = fb->height;
    f.numero  = ++numero_;
    ultima_   = proxima_;
    proxima_  = (proxima_ + 1) % GUARDADAS;
    esp_camera_fb_return(fb);
    return true;
  }

  // A mais recente, ou nullptr se nenhuma foi tirada.
  const Foto* ultima() const { return numero_ > 0 ? &fotos_[ultima_] : nullptr; }

  // Uma foto pelo numero, se ainda estiver guardada.
  const Foto* foto(uint32_t numero) const {
    for (const Foto& f : fotos_) {
      if (f.buf != nullptr && f.numero == numero) return &f;
    }
    return nullptr;
  }

  // Os numeros guardados, da mais nova para a mais velha.
  uint8_t numeros(uint32_t* saida) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < GUARDADAS; i++) {
      const Foto& f = fotos_[(ultima_ + GUARDADAS - i) % GUARDADAS];
      if (f.buf == nullptr) break;
      saida[n++] = f.numero;
    }
    return n;
  }

  // A mais recente, campo a campo.
  const uint8_t* buffer() const { return ultima() ? ultima()->buf : nullptr; }
  size_t tamanho() const { return ultima() ? ultima()->tamanho : 0; }
  uint16_t largura() const { return ultima() ? ultima()->largura : 0; }
  uint16_t altura() const { return ultima() ? ultima()->altura : 0; }

private:
  bool pronto_    = false;
  esp_err_t erro_ = ESP_OK;

  Foto fotos_[GUARDADAS];
  uint8_t proxima_ = 0;  // onde a proxima captura entra
  uint8_t ultima_  = 0;  // onde esta a mais recente
  uint32_t numero_ = 0;  // quantas ja foram tiradas neste boot
};

}  // namespace cerebro
