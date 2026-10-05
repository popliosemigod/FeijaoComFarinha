// =====================================================================
//  config_corpo.h - o corpo: pinagem, canais e parametros
//
//  Logica NAO mora aqui. Trocar de placa tem que ser mexer em um
//  arquivo so - e a regra do laboratorio, e e o que mantem firmware
//  legivel depois de crescer.
//
//  O corpo nao decide nada. Ele recebe comando pela UART, obedece
//  dentro do que o hardware aguenta, e para sozinho se o cerebro calar.
//  A unica outra voz que ele ouve e a do controle de PS4, e ela tambem
//  passa pelo failsafe.
//
//  DUAS PLACAS PODEM SER O CORPO, e so a pinagem muda:
//
//    (padrao)     ESP32 DevKit (ESP32 classico)     ambiente `corpo`
//    -DCORPO_C3   ESP32-C3 SuperMini                ambiente `corpo_c3`
//
//  A DevKit virou o corpo em 05/10/2026 por um motivo so: o controle
//  de PS4 fala Bluetooth CLASSICO, e o ESP32 classico e o unico da
//  familia que ainda o tem. O C3 e o S3 so fazem BLE.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "protocolo.h"

// ---- Identidade -----------------------------------------------------
#define CORPO_NOME   "feijao-corpo"
#define CORPO_VERSAO "0.2.0"

#if !defined(CORPO_C3)

// ---- Pinagem: ESP32 DevKit ------------------------------------------
//
//  Escolhida para caber em poucos pedacos de header, e longe de todo
//  pino de strapping (0, 2, 5, 12, 15):
//
//    lado esquerdo, em sequencia   32 33 25 26 27   as duas pontes H
//    lado direito, lado a lado     19 18            os dois servos
//    lado direito, lado a lado     RX2 TX2 (16 17)  o enlace
//
//  Motores por ponte H BTS7960 (modulo IBT-2), uma por motor. Sentido e
//  velocidade saem do duty cycle em RPWM/LPWM; o EN so habilita.
#define PIN_MOTOR_ESQ_R 32  // RPWM da ponte esquerda
#define PIN_MOTOR_ESQ_L 33  // LPWM da ponte esquerda
#define PIN_MOTOR_DIR_R 25  // RPWM da ponte direita
#define PIN_MOTOR_DIR_L 26  // LPWM da ponte direita

// R_EN e L_EN das DUAS pontes ligados juntos neste pino, com resistor
// de 10k para o GND. O pull-down e o que mantem as pontes desligadas
// durante o boot, quando o pino ainda esta em alta impedancia - sem
// ele, o robo pode dar um tranco ao ligar.
#define PIN_MOTOR_EN 27

#define PIN_SERVO_1 19
#define PIN_SERVO_2 18

// Enlace com o cerebro nos pinos que a DevKit ja traz marcados RX2 e
// TX2. Cruzado: o TX daqui vai no RX de la. O console fica no UART0,
// que e o USB da placa (o CH340/CP2102) - os dois nunca se misturam.
#define PIN_UART_RX 16
#define PIN_UART_TX 17

#else

// ---- Pinagem: ESP32-C3 SuperMini (o corpo ate 05/10/2026) -----------
#define PIN_MOTOR_ESQ_R 0
#define PIN_MOTOR_ESQ_L 1
#define PIN_MOTOR_DIR_R 3
#define PIN_MOTOR_DIR_L 4
#define PIN_MOTOR_EN    10  // com 10k para o GND, pelo mesmo motivo de cima
#define PIN_SERVO_1     5
#define PIN_SERVO_2     6

// ATENCAO, E NAO E DETALHE: 20 e 21 sao os pinos do UART0, o console
// padrao da placa. Isto so funciona porque o log sai pelo USB CDC
// (`ARDUINO_USB_CDC_ON_BOOT=1` no platformio.ini). Sem CDC o log e o
// enlace dividem o fio, e o cerebro recebe texto de boot no lugar de
// resposta - defeito que parece ruido eletrico e nao e.
#define PIN_UART_RX     20
#define PIN_UART_TX     21

#endif

// ---- Controle de PS4 ------------------------------------------------
//
//  So na DevKit: o C3 nao tem Bluetooth classico.
//
//  O controle de PS4 so se conecta ao endereco Bluetooth que ele
//  guardou - o do console com que foi pareado. Dois jeitos de ele
//  aceitar o robo, e `scripts/pareia_ps4.py` faz qualquer um deles com
//  o controle no USB do PC:
//
//    1. gravar no controle o endereco desta placa (PS4_MAC vazio)
//    2. ler o endereco que o controle ja guarda e por em PS4_MAC: a
//       placa passa a usa-lo, e o controle continua pareado ao console
//
//  PS4_MAC pode vir de `secrets.h`, como as credenciais do cerebro -
//  nao e segredo, mas e de cada controle, e nao do projeto.
#if !defined(CORPO_C3)
#define TEM_PS4 1
#else
#define TEM_PS4 0
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef PS4_MAC
#define PS4_MAC ""
#endif

// Raio morto dos manches, de 127. Abaixo disto o manche esta "no
// centro": todo controle tem folga, e sem raio morto o robo anda
// sozinho alguns milimetros por segundo.
#define PS4_RAIO_MORTO 14

// Por quanto tempo depois do ultimo toque o controle continua com a
// prioridade sobre o cerebro. Ver `main_corpo.cpp`.
#define PS4_PRIORIDADE_MS 500

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
//  de 19 us - visivel como tranco em movimento lento. A DevKit iria
//  alem; ficou igual para os dois corpos se moverem igual.
#define PWM_SERVO_HZ    50
#define PWM_SERVO_BITS  14
#define SERVO_PULSO_MIN 500   // us, correspondente a 0 grau
#define SERVO_PULSO_MAX 2400  // us, correspondente a 180 graus
#define SERVO_REPOUSO   90    // onde os dois ficam no boot

// No C3 os 6 canais do LEDC estao todos comprometidos: 4 nos motores e
// 2 nos servos. A DevKit tem 16, e sobram 10.

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
// O console sai pelo USB. Quando nao ha USB conectado, escrever nele
// nao bloqueia. E o que permite deixar o log ligado no robo andando
// pela sala.
#define LOG_BAUD 115200
