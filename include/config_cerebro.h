// =====================================================================
//  config_cerebro.h - qual placa e o cerebro
//
//  DUAS PLACAS PODEM SER O CEREBRO, e o resto do firmware nao precisa
//  saber qual. Este arquivo escolhe a pinagem; `camera.h`,
//  `microfone.h`, `audio.h` e `main_cerebro.cpp` compilam iguais nos
//  dois casos.
//
//    -DCEREBRO_CAM   ESP32-CAM (AI-Thinker), com INMP441 externo
//    (padrao)        XIAO ESP32-S3 Sense
//
//  Por que existem os dois caminhos: ha uma unica XIAO S3 Sense no
//  laboratorio, e tres projetos a querem. A ESP32-CAM e o plano que
//  destrava este aqui enquanto a Sense estiver em outro. Ela e pior -
//  a secao de ressalvas do `config_cerebro_cam.h` diz exatamente em
//  que -, e roda.
//
//  Cada um dos dois arquivos define o MESMO conjunto de nomes. Quando
//  um pino mudar, ele muda so la.
// =====================================================================
#pragma once

#if defined(CEREBRO_CAM)
#include "config_cerebro_cam.h"
#else
#include "config_cerebro_xiao.h"
#endif
