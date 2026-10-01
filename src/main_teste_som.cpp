// =====================================================================
//  main_teste_som.cpp - o cerebro: o teste simples, som -> motor
//
//  So tres pecas, e mais nada: o microfone, o enlace com o corpo e as
//  pontes H do outro lado. Sem camera, sem Wi-Fi, sem servo, sem voz.
//
//  Roda nas duas placas que podem ser o cerebro, e a diferenca inteira
//  e o microfone:
//
//    XIAO S3 Sense   PDM embutido, mono          (ambiente teste_som)
//    ESP32-CAM       INMP441 externo, estereo    (ambiente teste_som_cam)
//
//  A logica inteira cabe numa frase: UM SOM ALTO (uma palma) FAZ O
//  ROBO ANDAR PARA A FRENTE POR 1,5 s E PARAR.
//
//  E o menor teste que prova as tres coisas que ainda nao tinham
//  prova nenhuma: que o microfone ouve, que o enlace entrega, e que o
//  motor gira. Cada uma aparece separada no console, para que um
//  defeito numa nao se esconda atras da outra:
//
//    mic E / D     nivel do microfone, de 0 a 1 (na XIAO, so o E)
//    corpo         respondendo ou MUDO, com a contagem de respostas
//    estado        ouvindo, andando ou descansando
//
//  Na ESP32-CAM o INMP441 e lido em ESTEREO de proposito. O firmware
//  de verdade le so o canal esquerdo (L/R no GND); aqui os dois
//  aparecem, entao um INMP441 com o L/R solto ou no 3,3 V e visto em
//  vez de virar "o microfone nao funciona".
//
//  O corpo roda o firmware normal (`corpo`). O failsafe dele continua
//  valendo: se esta placa travar com o robo andando, ele para em 1 s.
//
//  TUDO ISTO TAMBEM SE LE E SE OPERA PELO USB DO CORPO: o relato sai
//  pelo enlace (`#...`, que o corpo mostra como `[cam] ...`) e as
//  teclas chegam por ele (`>g` no console do corpo). Na XIAO, que tem
//  USB proprio, isso e conforto - um cabo so basta. Na ESP32-CAM e o
//  unico caminho: o adaptador dela ocupa o header inteiro, e por isso
//  ate o firmware novo entra pelo enlace
//  (`scripts/grava_pelo_enlace.py`).
// =====================================================================
#include <Arduino.h>
#include <ESP_I2S.h>

#include <atomic>
#include <stdarg.h>

#include "atualiza_serial.h"
#include "config_cerebro.h"
#include "corpo_link.h"
#include "protocolo.h"

namespace {

// ---- Os numeros do teste --------------------------------------------
const int VELOCIDADE     = 40;    // % - baixo, porque o robo esta na mesa
const uint32_t ANDA_MS   = 1500;  // quanto tempo anda depois do som
const uint32_t PAUSA_MS  = 1500;  // surdo depois de andar: o motor faz barulho
const uint32_t RELATO_MS = 500;

// O som dispara quando passa do piso absoluto E de algumas vezes o
// ruido de fundo. So o absoluto dispararia numa sala barulhenta; so o
// relativo dispararia com qualquer estalo numa sala em silencio.
float limiar_minimo      = 0.02f;  // teclas + e - mudam na hora
const float LIMIAR_VEZES = 5.0f;

// Desarmado, o som continua sendo medido e relatado, mas nao move o
// robo. E como se ajusta o limiar sem o robo andando a cada tentativa.
bool armado = true;

const size_t QUADROS = 256;  // 16 ms a 16 kHz

cerebro::Corpo corpo;
I2SClass i2s;
bool tem_mic = false;

#if MIC_PDM
int16_t cru[QUADROS];  // mono: o periferico ja entrega PCM de 16 bits
#else
int32_t cru[QUADROS * 2];  // E, D, E, D...
#endif

enum class Estado : uint8_t {
  OUVINDO,
  ANDANDO,
  DESCANSO
};
Estado estado        = Estado::DESCANSO;  // o microfone liga com um estalo: descarta
uint32_t desde_ms    = 0;
uint32_t disparos    = 0;
float fundo          = 0.0f;  // ruido de fundo, media lenta
float pico_esq       = 0.0f;  // maiores niveis desde o ultimo relato
float pico_dir       = 0.0f;
bool chegou_nao_zero = false;  // algum bit do microfone ja mexeu?

// O que o diagnostico do enlace achou no boot, guardado para ser dito
// DEPOIS que o enlace subir - antes disso nao ha por onde dizer.
bool enlace_de_pe   = false;
bool boot_pong      = false;
uint32_t boot_no_rx = 0;
uint32_t boot_no_tx = 0;

// Linha `>...` vinda do USB do corpo. Chega na task do enlace e e
// consumida no loop(): uma por vez, e a que chegar com a anterior
// ainda pendente se perde - e console, nao fila de comando.
char linha_tunel[protocolo::LINHA_MAX] = {0};
std::atomic<bool> tem_linha_tunel{false};

void recebeTunel(const char* linha) {
  if (tem_linha_tunel) return;
  strlcpy(linha_tunel, linha, sizeof(linha_tunel));
  tem_linha_tunel = true;
}

// Diz a mesma coisa nos dois consoles: o da propria placa (USB na
// XIAO, UART0 com adaptador na CAM) e o enlace, que o corpo repassa ao
// USB dele.
void diz(const char* formato, ...) {
  char texto[128];
  va_list args;
  va_start(args, formato);
  vsnprintf(texto, sizeof(texto), formato, args);
  va_end(args);
  Serial.println(texto);
  if (enlace_de_pe) corpo.relata(texto);
}

const char* nome(Estado e) {
  switch (e) {
    case Estado::OUVINDO: return "ouvindo";
    case Estado::ANDANDO: return "ANDANDO";
    default: return "descansando";
  }
}

// Nivel de um bloco, de 0 a 1, sem a componente continua: os dois
// microfones tem um offset DC que sozinho ja pareceria som.
//
// `passo` e `desloca` sao o que muda entre eles. No INMP441 as
// amostras sao de 32 bits intercaladas (passo 2) com o audio nos bits
// de cima (desloca 16); no PDM sao de 16 bits, uma atras da outra.
template <typename T>
float nivelBloco(const T* amostras, size_t quadros, uint8_t passo, uint8_t desloca) {
  int64_t soma = 0;
  for (size_t i = 0; i < quadros; i++) soma += (amostras[i * passo] >> desloca);
  const int32_t media = (int32_t)(soma / (int64_t)quadros);

  uint64_t desvio = 0;
  for (size_t i = 0; i < quadros; i++) {
    desvio += (uint64_t)abs((int32_t)(amostras[i * passo] >> desloca) - media);
  }
  return (float)(desvio / quadros) / 32768.0f;
}

// ---- Diagnostico do enlace -----------------------------------------
//
//  Enlace mudo nao diz qual fio falhou, e os dois defeitos comuns sao
//  identicos de fora: fio solto, e fios invertidos (TX no TX). Aqui os
//  dois se separam, em duas etapas:
//
//    1. PING nos pinos certos. Se o PONG volta, acabou.
//    2. Se nao volta, o cerebro SO ESCUTA, um pino de cada vez, sem dirigir
//       nenhum. Quem estiver no console USB do corpo manda `PING`
//       nessa janela, e o PONG aparece no pino em que o TX do corpo
//       realmente esta.
//
//  Medir a tensao de repouso nao serve: os pinos do cartao SD da
//  ESP32-CAM tem pull-up na placa, e um pino solto le alto igual a um
//  pino ligado num TX. Foi a primeira tentativa, e ela mentiu.
bool pong(int8_t rx, int8_t tx) {
  Serial1.begin(protocolo::BAUD, SERIAL_8N1, rx, tx);
  delay(20);
  while (Serial1.available()) Serial1.read();
  Serial1.print("PING\n");
  char visto[8]     = {0};
  uint8_t n         = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < 300) {
    if (!Serial1.available()) continue;
    const char c = (char)Serial1.read();
    if (n < sizeof(visto) - 1) visto[n++] = c;
    if (strstr(visto, "PONG") != nullptr) return true;
  }
  return false;
}

// Bytes que chegam num pino em `ms`, sem transmitir nada.
uint32_t escuta(int8_t pino, uint32_t ms) {
  Serial1.end();
  Serial1.begin(protocolo::BAUD, SERIAL_8N1, pino, -1);
  delay(20);
  while (Serial1.available()) Serial1.read();
  uint32_t bytes    = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (Serial1.available()) {
      Serial1.read();
      bytes++;
    }
    delay(2);
  }
  Serial1.end();
  return bytes;
}

// Devolve true se o enlace deve subir com os pinos trocados.
bool diagnosticaEnlace() {
  if (pong(PIN_UART_RX, PIN_UART_TX)) {
    Serial.println("[enlace] PING -> PONG: enlace ok nos pinos certos");
    boot_pong = true;
    return false;
  }
  Serial.println("[enlace] PING sem PONG. Escutando 2 s em cada pino - mande PING no USB do corpo");
  const uint32_t no_rx = escuta(PIN_UART_RX, 2000);
  const uint32_t no_tx = escuta(PIN_UART_TX, 2000);
  boot_no_rx           = no_rx;
  boot_no_tx           = no_tx;
  Serial.printf("[enlace] bytes do corpo: %lu no GPIO%d (nosso RX) | %lu no GPIO%d (nosso TX)\n",
                (unsigned long)no_rx, PIN_UART_RX, (unsigned long)no_tx, PIN_UART_TX);

  if (no_tx > 0 && no_rx == 0) {
    Serial.println("[enlace] *** FIOS INVERTIDOS: o TX do corpo esta no nosso TX. ***");
    Serial.println("[enlace] *** O teste segue trocado; o firmware de verdade NAO. ***");
    return true;
  }
  if (no_rx > 0) {
    Serial.printf("[enlace] o corpo chega ate aqui; quem falha e o nosso GPIO%d -> GPIO20 do C3\n",
                  PIN_UART_TX);
  } else {
    Serial.println("[enlace] o TX do corpo (GPIO21 do C3) nao chega em nenhum dos dois pinos");
  }
  return false;
}

// ---- O microfone ---------------------------------------------------
#if MIC_PDM

bool sobeMicrofone() {
  i2s.end();
  i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  return i2s.begin(I2S_MODE_PDM_RX, MIC_TAXA_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
}

// Le um bloco e devolve o nivel. False se nao veio amostra nenhuma.
bool leMicrofone(float& esq, float& dir) {
  const size_t quadros = i2s.readBytes((char*)cru, sizeof(cru)) / sizeof(int16_t);
  if (quadros == 0) return false;
  for (size_t i = 0; i < quadros && !chegou_nao_zero; i++) {
    if (cru[i] != 0 && cru[i] != -1) chegou_nao_zero = true;
  }
  esq = nivelBloco(cru, quadros, 1, 0);
  dir = 0.0f;  // mono: nao ha canal direito
  return true;
}

void varreMicrofone() {
  diz("[mic] PDM embutido na placa: nao ha pino a varrer");
}

#else

bool sobeMicrofone(int8_t bclk = PIN_MIC_BCLK, int8_t ws = PIN_MIC_WS, int8_t sd = PIN_MIC_DATA) {
  i2s.end();
  i2s.setPins(bclk, ws, -1, sd, -1);
  return i2s.begin(I2S_MODE_STD, MIC_TAXA_HZ, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
}

bool leMicrofone(float& esq, float& dir) {
  const size_t quadros = i2s.readBytes((char*)cru, sizeof(cru)) / (2 * sizeof(int32_t));
  if (quadros == 0) return false;
  for (size_t i = 0; i < quadros * 2 && !chegou_nao_zero; i++) {
    if (cru[i] != 0 && cru[i] != -1) chegou_nao_zero = true;
  }
  esq = nivelBloco(cru, quadros, 2, 16);
  dir = nivelBloco(cru + 1, quadros, 2, 16);
  return true;
}

// Tenta o INMP441 em todas as combinacoes dos pinos livres. So zeros
// nos pinos do config pode ser microfone ausente ou tres fios em
// ordem trocada, e de fora as duas coisas sao identicas - aqui nao.
//
// GPIO13 e 14 ficam fora: sao do enlace. O GPIO4 (LED de flash) entra
// so como dado, nunca como relogio.
void varreMicrofone() {
  static const int8_t RELOGIOS[] = {PIN_MIC_BCLK, PIN_MIC_WS, PIN_MIC_DATA};
  static const int8_t DADOS[]    = {PIN_MIC_BCLK, PIN_MIC_WS, PIN_MIC_DATA, 4};
  diz("[mic] varrendo: BCLK WS SD -> a linha de dados");
  bool achou = false;
  for (int8_t b : RELOGIOS) {
    for (int8_t w : RELOGIOS) {
      if (w == b) continue;
      for (int8_t d : DADOS) {
        if (d == b || d == w) continue;
        if (!sobeMicrofone(b, w, d)) {
          diz("[mic] %2d %2d %2d I2S nao subiu", b, w, d);
          continue;
        }
        // O INMP441 leva ~250 ms de relogio para acordar: le e joga fora.
        for (uint8_t i = 0; i < 25; i++) i2s.readBytes((char*)cru, sizeof(cru));
        uint32_t zeros = 0, uns = 0, outros = 0;
        float maior = 0.0f;
        for (uint8_t i = 0; i < 8; i++) {
          const size_t q = i2s.readBytes((char*)cru, sizeof(cru)) / (2 * sizeof(int32_t));
          for (size_t k = 0; k < q * 2; k++) {
            // Audio mexe nos 24 bits de cima. So o ultimo bit mudando
            // e a linha solta pegando a borda do relogio vizinho.
            const int32_t alto = cru[k] >> 8;
            if (alto == 0)
              zeros++;
            else if (alto == -1)
              uns++;
            else
              outros++;
          }
          if (q == 0) continue;
          const float e = nivelBloco(cru, q, 2, 16), r = nivelBloco(cru + 1, q, 2, 16);
          if (e > maior) maior = e;
          if (r > maior) maior = r;
        }
        const bool sinal = outros > (zeros + uns) / 10;
        diz("[mic] %2d %2d %2d %s niv %.4f 0x%08lX", b, w, d,
            sinal ? "SINAL <--" : (uns > zeros ? "preso em 1" : "so zeros"), maior,
            (unsigned long)cru[0]);
        achou |= sinal;
      }
    }
  }
  if (!achou) {
    diz("[mic] nenhuma combinacao deu sinal: INMP441 sem");
    diz("[mic] alimentacao ou fora destes pinos (VDD 3V3, GND)");
  }
  tem_mic         = sobeMicrofone();
  chegou_nao_zero = false;
}

#endif  // MIC_PDM

void anda() {
  corpo.anda(VELOCIDADE, VELOCIDADE);
  estado   = Estado::ANDANDO;
  desde_ms = millis();
  disparos++;
}

void para() {
  corpo.para();
  estado   = Estado::DESCANSO;
  desde_ms = millis();
}

void relato() {
  Serial.printf(
      "mic E %.4f D %.4f  fundo %.4f%s | corpo %s (%lu env, %lu resp) | %s, %lu disparos\n",
      pico_esq, pico_dir, fundo, chegou_nao_zero ? "" : "  SO ZEROS",
      corpo.vivo() ? "respondendo" : "MUDO", (unsigned long)corpo.enviados(),
      (unsigned long)corpo.respostas(), nome(estado), (unsigned long)disparos);

  // A mesma coisa pelo enlace, encolhida para caber numa linha do
  // protocolo. `r` sao as respostas do corpo: se ficar em zero, o
  // sentido corpo -> cerebro do fio nao esta chegando.
  char curto[protocolo::LINHA_MAX];
  snprintf(curto, sizeof(curto), "mic E%.4f D%.4f f%.4f l%.3f %s%s d%lu r%lu", pico_esq, pico_dir,
           fundo, limiar_minimo, chegou_nao_zero ? "" : "ZEROS ",
           armado ? nome(estado) : "desarmado", (unsigned long)disparos,
           (unsigned long)corpo.respostas());
  if (enlace_de_pe) corpo.relata(curto);
  pico_esq = pico_dir = 0.0f;
}

void apresenta() {
  diz("teste_som de " __DATE__ " " __TIME__);
  if (boot_pong) {
    diz("[enlace] PING -> PONG ok no boot");
  } else {
    diz("[enlace] sem PONG no boot (ouviu rx %lu tx %lu)", (unsigned long)boot_no_rx,
        (unsigned long)boot_no_tx);
  }
  diz("[mic] %s %s, limiar %.3f, %s", MIC_PDM ? "PDM embutido" : "INMP441",
      tem_mic ? "ok" : "FALHOU", limiar_minimo, armado ? "armado" : "desarmado");
  diz("teclas: g anda | x para | d arma/desarma | + - limiar");
  diz("        v varre o mic | ? isto | U grava firmware");
}

// Uma tecla do console, venha da propria placa ou do USB do corpo (`>tecla`).
void tecla(char c) {
  switch (c) {
    case 'g': anda(); break;
    case 'x': para(); break;
    case 'd':
      armado = !armado;
      diz("[teste] som %s", armado ? "ARMADO: move o robo" : "desarmado: so mede");
      break;
    case '+':
      limiar_minimo *= 1.5f;
      diz("[teste] limiar %.4f", limiar_minimo);
      break;
    case '-':
      limiar_minimo /= 1.5f;
      diz("[teste] limiar %.4f", limiar_minimo);
      break;
    case 'v':
      if (estado != Estado::ANDANDO) varreMicrofone();
      break;
    case '?': apresenta(); break;
    default: break;
  }
}

// Firmware novo chegando por `s`. Pelo enlace, a task do heartbeat sai
// do caminho antes: o fio passa a ser so da gravacao. O corpo fica sem
// noticia e para sozinho - que e o estado certo para um robo cujo
// cerebro esta se regravando.
void gravaFirmware(Stream& s, bool pelo_enlace, size_t tamanho, const char* md5) {
  para();
  if (pelo_enlace) corpo.pausa();
  if (cerebro::atualizaPorSerial(s, tamanho, md5)) {
    delay(300);
    ESP.restart();
  }
  if (pelo_enlace) corpo.retoma();
}

// Uma linha de console: ou e o comando de gravacao, ou sao teclas.
void linhaDeConsole(Stream& origem, bool pelo_enlace, const char* linha) {
  size_t tamanho = 0;
  char md5[33];
  if (cerebro::leComandoAtualiza(linha, tamanho, md5)) {
    gravaFirmware(origem, pelo_enlace, tamanho, md5);
    return;
  }
  for (const char* p = linha; *p != '\0'; p++) tecla(*p);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t ate = millis() + 2000;
  while (!Serial && millis() < ate) delay(10);  // da tempo de o USB aparecer
  Serial.printf("\n=== %s teste_som: palma -> anda %lu ms a %d%% ===\n", CEREBRO_NOME,
                (unsigned long)ANDA_MS, VELOCIDADE);

  // O enlace primeiro, como no firmware de verdade: o corpo ja recebe
  // `M 0 0` enquanto o resto sobe.
  const bool trocado = diagnosticaEnlace();
  corpo.aoConsole(recebeTunel);
  enlace_de_pe = trocado ? corpo.begin(PIN_UART_TX, PIN_UART_RX) : corpo.begin();
  Serial.printf("[teste] enlace: %s\n", enlace_de_pe ? "de pe" : "FALHOU");

#ifdef MIC_PORTA_I2S
  i2s.setPort(MIC_PORTA_I2S);  // na CAM o I2S0 e da camera - ver config_cerebro_cam.h
#endif
  tem_mic = sobeMicrofone();

  apresenta();
  desde_ms = millis();
}

void loop() {
  // ---- Console: o caminho que prova o motor sem depender do som ---
  // Aqui as teclas valem sem Enter. So o `U` espera o resto da linha,
  // porque leva tamanho e MD5.
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == 'U') {
      char linha[protocolo::LINHA_MAX] = {'U'};
      size_t n                         = Serial.readBytesUntil('\n', linha + 1, sizeof(linha) - 2);
      if (n > 0 && linha[n] == '\r') n--;
      linha[1 + n] = '\0';
      linhaDeConsole(Serial, false, linha);
    } else {
      tecla(c);
    }
  }

  // O mesmo console, vindo do USB do corpo pelo enlace.
  if (tem_linha_tunel) {
    char linha[protocolo::LINHA_MAX];
    strlcpy(linha, linha_tunel, sizeof(linha));
    tem_linha_tunel = false;
    linhaDeConsole(Serial1, true, linha);
  }

  // ---- Microfone --------------------------------------------------
  float nivel = 0.0f;
  if (tem_mic) {
    float esq = 0.0f, dir = 0.0f;
    if (leMicrofone(esq, dir)) {
      if (esq > pico_esq) pico_esq = esq;
      if (dir > pico_dir) pico_dir = dir;
      nivel = esq > dir ? esq : dir;
    }
  } else {
    delay(16);
  }

  // ---- A logica: tres estados -------------------------------------
  const uint32_t agora = millis();
  switch (estado) {
    case Estado::OUVINDO:
      if (nivel > limiar_minimo && nivel > fundo * LIMIAR_VEZES) {
        diz("[teste] som! nivel %.4f (fundo %.4f)%s", nivel, fundo, armado ? " - andando" : "");
        if (armado) {
          anda();
        } else {
          // Desarmado tambem descansa: senao um som longo vira uma
          // linha de log por bloco de 16 ms.
          estado   = Estado::DESCANSO;
          desde_ms = millis();
        }
      } else {
        fundo += (nivel - fundo) * 0.02f;
      }
      break;

    case Estado::ANDANDO:
      if (agora - desde_ms >= ANDA_MS) {
        diz("[teste] parando");
        para();
      }
      break;

    case Estado::DESCANSO:
      // O descanso tambem APRENDE o ruido de fundo, e rapido. Sem isso
      // um nivel alto e constante (sala barulhenta, microfone ruidoso)
      // disparava de novo no instante em que voltava a ouvir, para
      // sempre: o fundo so era aprendido quando nao havia disparo, e
      // nunca deixava de haver. Visto na bancada em 30/09/2026 - o
      // corpo recebia um STOP a cada 3 s exatos.
      //
      // Os primeiros 500 ms ficam de fora: e o motor ainda parando.
      if (agora - desde_ms >= 500) fundo += (nivel - fundo) * 0.05f;
      if (agora - desde_ms >= PAUSA_MS) estado = Estado::OUVINDO;
      break;
  }

  static uint32_t ultimo_relato = 0;
  if (agora - ultimo_relato >= RELATO_MS) {
    ultimo_relato = agora;
    relato();
  }
}
