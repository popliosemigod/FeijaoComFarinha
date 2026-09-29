// =====================================================================
//  camera.h - a camera da placa Sense, ligada e testavel
//
//  Sem funcionalidade decidida ainda, por escolha do projeto: o que se
//  pediu nesta etapa foi deixa-la PRONTA e verificavel. Entao ela
//  inicializa, tira um quadro e diz o tamanho - o suficiente para
//  provar que o conector esta encaixado e que a PSRAM esta habilitada,
//  que sao os dois erros que aparecem primeiro.
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
    return pronto_;
  }

  bool pronto() const { return pronto_; }
  esp_err_t erro() const { return erro_; }

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

private:
  bool pronto_    = false;
  esp_err_t erro_ = ESP_OK;
};

}  // namespace cerebro
