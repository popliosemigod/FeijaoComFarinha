// =====================================================================
//  config_corpo.h - ESP32-C3 SuperMini: pinagem, canais e parametros
//
//  Logica NAO mora aqui. Trocar de placa tem que ser mexer em um
//  arquivo so - e a regra do laboratorio, e e o que mantem firmware
//  legivel depois de crescer.
//
//  O corpo nao decide nada. Ele recebe comando pela UART, obedece
//  dentro do que o hardware aguenta, e para sozinho se o cerebro calar.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "protocolo.h"

// ---- Identidade -----------------------------------------------------
#define CORPO_NOME   "feijao-corpo"
#define CORPO_VERSAO "0.1.0"

// ---- Pinagem: ESP32-C3 SuperMini ------------------------------------
//
//  Motores por ponte H BTS7960 (modulo IBT-2), uma por motor. Sentido e
//  velocidade saem do duty cycle em RPWM/LPWM; o EN so habilita.
#define PIN_MOTOR_ESQ_R 0  // RPWM da ponte esquerda
#define PIN_MOTOR_ESQ_L 1  // LPWM da ponte esquerda
#define PIN_MOTOR_DIR_R 3  // RPWM da ponte direita
#define PIN_MOTOR_DIR_L 4  // LPWM da ponte direita

// R_EN e L_EN das DUAS pontes ligados juntos neste pino, com resistor
// de 10k para o GND. O pull-down e o que mantem as pontes desligadas
// durante o boot, quando o pino ainda esta em alta impedancia - sem
// ele, o robo pode dar um tranco ao ligar.
#define PIN_MOTOR_EN 10

#define PIN_SERVO_1 5
#define PIN_SERVO_2 6

// UART1 para o cerebro. Cruzada: o TX daqui vai no RX de la.
//
//  ATENCAO, E NAO E DETALHE: 20 e 21 sao os pinos do UART0, o console
//  padrao da placa. Isto so funciona porque o log de depuracao sai pelo
//  USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1` no platformio.ini). Compilar
//  sem CDC poe o log e o enlace no mesmo fio, e o cerebro passa a
//  receber texto de boot no lugar de resposta - defeito que parece
//  ruido eletrico e nao e.
#define PIN_UART_RX 20
#define PIN_UART_TX 21

// ---- PWM dos motores ------------------------------------------------
//
//  20 kHz fica acima da audicao: em 1 kHz a ponte canta, e num robo de
//  sala isso incomoda mais do que parece. 10 bits em 20 kHz pede
//  20,5 MHz do periferico LEDC, que trabalha com 80 MHz - cabe folgado.
#define PWM_MOTOR_HZ   20000
#define PWM_MOTOR_BITS 10
#define PWM_MOTOR_MAX  ((1 << PWM_MOTOR_BITS) - 1)

// ---- PWM dos servos -------------------------------------------------
//
//  50 Hz e 14 bits. Os 14 bits nao sao exagero: sao o MAXIMO do LEDC do
//  C3, e em 50 Hz eles dao 1,2 us de passo. Com 10 bits o passo seria
//  de 19 us - visivel como tranco em movimento lento.
#define PWM_SERVO_HZ    50
#define PWM_SERVO_BITS  14
#define SERVO_PULSO_MIN 500   // us, correspondente a 0 grau
#define SERVO_PULSO_MAX 2400  // us, correspondente a 180 graus
#define SERVO_REPOUSO   90    // onde os dois ficam no boot

// Os 6 canais do LEDC do C3 estao todos comprometidos: 4 nos motores e
// 2 nos servos. Nao ha canal livre - qualquer PWM novo (buzzer, LED
// com brilho, fita) exige tirar outro daqui primeiro.

// ---- Rampa de aceleracao -------------------------------------------
//
//  DESLIGADA ATE HENRIQUE DECIDIR. O prompt do projeto pede para
//  discutir rampa antes de implementar, entao ela esta escrita e
//  inativa: com 0, `M` vale na hora, exatamente como o protocolo diz.
//
//  O argumento a favor de ligar: mandar um motor parado direto para
//  100% puxa a corrente de rotor travado, que num motor pequeno chega
//  a varias vezes a nominal. Com servo e ponte na mesma fonte, esse
//  pico e o caminho mais curto para um brownout - o robo reinicia no
//  meio do movimento e ninguem entende por que.
//
//  O argumento contra: rampa atrasa a parada de emergencia se for
//  aplicada tambem na descida. Por isso, se for ligada, ela sobe
//  devagar e desce na hora - ver motores.h.
//
//  Valor sugerido quando for ligar: 250 (ms de 0 a 100%).
#define RAMPA_SUBIDA_MS 0

// ---- Ritmo do laco --------------------------------------------------
// 10 ms da 100 Hz, que e o dobro da taxa do servo: suficiente para o
// failsafe reagir dentro do seu proprio milissegundo e barato o
// bastante para nao disputar com a UART.
#define PASSO_LACO_MS 10

// ---- Ponte USB <-> enlace -------------------------------------------
// Quanto tempo a ponte espera por um byte do USB antes de fechar. Tem
// que cobrir a conferencia final de uma gravacao (o cerebro relendo a
// imagem inteira para o MD5), em que o PC fica calado.
#define PONTE_OCIOSA_MS 5000

// ---- Log ------------------------------------------------------------
// O console sai pelo USB CDC. Quando nao ha USB conectado, escrever
// nele nao bloqueia - o core descarta. E o que permite deixar o log
// ligado no robo andando pela sala.
#define LOG_BAUD 115200
