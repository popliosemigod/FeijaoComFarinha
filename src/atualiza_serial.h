// =====================================================================
//  atualiza_serial.h - gravar firmware novo por uma serial qualquer
//
//  A ESP32-CAM so grava pelo UART0, e o adaptador que chega nele ocupa
//  o header inteiro: com ele encaixado, o microfone e o enlace saem.
//  Trocar o firmware com o robo montado ficava impossivel.
//
//  Isto resolve pelo lado de dentro: o firmware que ja esta rodando
//  recebe a imagem nova por um `Stream` - o enlace com o corpo, que e
//  o unico fio sempre ligado - e grava na outra particao OTA. Do lado
//  do PC, `scripts/grava_pelo_enlace.py`; no meio, o corpo em `PONTE`.
//
//  Nao depende de Wi-Fi nem de senha nenhuma, de proposito: e o
//  caminho que tem que funcionar justamente quando o resto nao subiu.
//
//  O PROTOCOLO, depois da linha `U <tamanho> <md5>`:
//
//     PC   0xA5 | offset (4, LE) | n (2, LE) | n bytes | CRC16 (2, LE)
//     CAM  `#U k <recebido>`   bloco gravado, manda o proximo
//          `#U n <motivo>`     bloco recusado, manda de novo
//          `#U ok`             imagem conferida (MD5), vai reiniciar
//          `#U erro <motivo>`  desistiu, o firmware atual continua
//
//  Um bloco por vez, com resposta: quem dita o ritmo e a CAM, entao a
//  gravacao na flash - que e lenta e para as interrupcoes - nunca
//  acontece com byte chegando. O offset no bloco e o que torna a
//  repeticao inofensiva: bloco ja gravado e so confirmado de novo.
//
//  SE DER ERRADO, NAO ESTRAGA. A imagem vai para a OUTRA particao, e a
//  troca so acontece depois de o MD5 da imagem inteira conferir. Queda
//  de energia no meio deixa o firmware atual intacto.
//
//  O que ela NAO protege: gravar um firmware que nao tenha este
//  arquivo dentro. Ele sobe, e o caminho de volta passa a ser so o
//  adaptador.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <Update.h>

namespace cerebro {

static const size_t ATUALIZA_BLOCO_MAX     = 1024;
static const uint32_t ATUALIZA_SILENCIO_MS = 8000;  // sem bloco nenhum: desiste
static const uint8_t ATUALIZA_FALHAS_MAX   = 20;    // blocos recusados seguidos

// CRC-16/CCITT-FALSE: o mesmo que `binascii.crc_hqx(dados, 0xFFFF)`.
inline uint16_t crc16(const uint8_t* dados, size_t n, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)dados[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// Le exatamente `n` bytes, ou desiste em `ms`.
inline bool leExato(Stream& s, uint8_t* destino, size_t n, uint32_t ms) {
  const uint32_t t0 = millis();
  size_t i          = 0;
  while (i < n) {
    if (s.available()) {
      destino[i++] = (uint8_t)s.read();
    } else {
      if (millis() - t0 > ms) return false;
      delay(1);
    }
  }
  return true;
}

// Joga fora o resto de um bloco quebrado, ate a linha calar.
inline void drena(Stream& s) {
  uint32_t ultimo = millis();
  while (millis() - ultimo < 60) {
    if (s.available()) {
      s.read();
      ultimo = millis();
    } else {
      delay(1);
    }
  }
}

// Recebe a imagem e grava. Devolve true quando a imagem nova esta
// conferida e pronta para o proximo boot - quem chama reinicia.
inline bool atualizaPorSerial(Stream& s, size_t tamanho, const char* md5) {
  if (!Update.begin(tamanho)) {
    s.printf("#U erro begin: %s\n", Update.errorString());
    return false;
  }
  if (!Update.setMD5(md5)) {
    Update.abort();
    s.print("#U erro md5 mal formado\n");
    return false;
  }
  s.print("#U pronto\n");

  static uint8_t bloco[6 + ATUALIZA_BLOCO_MAX];  // offset + n + dados
  size_t recebido = 0;
  uint8_t falhas  = 0;
  uint32_t ultimo = millis();

  while (recebido < tamanho) {
    if (falhas > ATUALIZA_FALHAS_MAX || millis() - ultimo > ATUALIZA_SILENCIO_MS) {
      Update.abort();
      s.printf("#U erro %s em %u\n", falhas > ATUALIZA_FALHAS_MAX ? "blocos ruins" : "silencio",
               (unsigned)recebido);
      return false;
    }
    if (!s.available()) {
      delay(1);
      continue;
    }
    if (s.read() != 0xA5) continue;  // procura o comeco de um bloco

    if (!leExato(s, bloco, 6, 500)) {
      falhas++;
      s.print("#U n cabecalho\n");
      continue;
    }
    const uint32_t offset = (uint32_t)bloco[0] | ((uint32_t)bloco[1] << 8) |
                            ((uint32_t)bloco[2] << 16) | ((uint32_t)bloco[3] << 24);
    const uint16_t n = (uint16_t)(bloco[4] | (bloco[5] << 8));
    uint8_t crc_rx[2];
    if (n == 0 || n > ATUALIZA_BLOCO_MAX || !leExato(s, bloco + 6, n, 2000) ||
        !leExato(s, crc_rx, 2, 500)) {
      falhas++;
      drena(s);
      s.print("#U n tamanho\n");
      continue;
    }
    if (crc16(bloco, 6 + (size_t)n) != (uint16_t)(crc_rx[0] | (crc_rx[1] << 8))) {
      falhas++;
      drena(s);
      s.print("#U n crc\n");
      continue;
    }
    ultimo = millis();

    if (offset < recebido) {
      // Repeticao de bloco ja gravado: a nossa resposta se perdeu.
      s.printf("#U k %u\n", (unsigned)recebido);
      continue;
    }
    if (offset > recebido || recebido + n > tamanho) {
      falhas++;
      s.printf("#U n offset %u\n", (unsigned)recebido);
      continue;
    }
    if (Update.write(bloco + 6, n) != n) {
      s.printf("#U erro write: %s\n", Update.errorString());
      Update.abort();
      return false;
    }
    recebido += n;
    falhas = 0;
    s.printf("#U k %u\n", (unsigned)recebido);
  }

  if (!Update.end(true)) {
    s.printf("#U erro end: %s\n", Update.errorString());
    return false;
  }
  s.print("#U ok\n");
  s.flush();
  return true;
}

// Interpreta `U <tamanho> <md5>` (a linha sem a marca). False se nao for.
inline bool leComandoAtualiza(const char* linha, size_t& tamanho, char md5[33]) {
  if (linha[0] != 'U' || linha[1] != ' ') return false;
  unsigned long t = 0;
  char m[33]      = {0};
  if (sscanf(linha + 2, "%lu %32s", &t, m) != 2 || t == 0 || strlen(m) != 32) return false;
  tamanho = (size_t)t;
  memcpy(md5, m, 33);
  return true;
}

}  // namespace cerebro
