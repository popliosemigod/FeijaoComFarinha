// =====================================================================
//  controle_ps4.h - o controle de PS4, traduzido para o corpo
//
//  O controle e a segunda voz que o corpo ouve, e a unica que nao vem
//  do cerebro. Ele fala Bluetooth classico direto com esta placa: o
//  cerebro nem fica sabendo, e por isso o controle continua valendo
//  com o cerebro travado, desligado ou regravando a si proprio.
//
//  O mapeamento e o menor que se dirige sem manual:
//
//    manche esquerdo   frente/tras anda, esquerda/direita vira
//    O (bolinha)       freia, enquanto apertado
//    X (cruz)          tira uma foto (quem tira e o cerebro)
//    manche direito    move os servos 1 (lados) e 2 (cima/baixo); solto,
//                      eles ficam onde estao
//    R3                servos de volta ao repouso
//
//  As contas que transformam os manches em rodas e servos (`mistura`,
//  `passoDoServo`) sao puras e ficam fora do bloco do Bluetooth: o
//  autoteste as exercita numa placa sem controle nenhum.
// =====================================================================
#pragma once

#include <Arduino.h>

#include <atomic>

#include "config_corpo.h"
#include "protocolo.h"

#if TEM_PS4 && !defined(FEIJAO_AUTOTESTE)
#include <PS4Controller.h>
#include <Preferences.h>
#include <esp_gap_bt_api.h>
#define FEIJAO_PS4_LIGADO 1

// Da pilha Bluedroid do ESP-IDF, fora da API publica - a biblioteca do
// PS4 ja usa o L2CAP dela do mesmo jeito. Assinatura do ESP-IDF 5.5
// (bta_api.h): endereco, classe, chave, servicos, confiavel, tipo de
// chave, capacidade de E/S, tamanho do PIN, conexao segura. Manda um
// pedido para a task do Bluetooth em vez de mexer na tabela dela daqui.
extern "C" void BTA_DmAddDevice(uint8_t* bd_addr, uint8_t* dev_class, uint8_t* link_key,
                                uint32_t trusted_mask, uint8_t is_trusted, uint8_t key_type,
                                uint8_t io_cap, uint8_t pin_length, uint8_t sc_support);
#else
#define FEIJAO_PS4_LIGADO 0
#endif

namespace corpo {

class ControlePS4 {
public:
  // ---- A conta, sem hardware ---------------------------------------
  //
  // `x` e `y` de -127 a 127, com y positivo PARA A FRENTE. Devolve a
  // velocidade das duas rodas, de -100 a 100, no estilo "arcade": somar
  // a virada numa roda e tirar da outra. Dentro do raio morto o manche
  // vale zero - e devolve false, para quem chama saber que ele esta no
  // centro.
  static bool mistura(int x, int y, int& esq, int& dir) {
    if (abs(x) < PS4_RAIO_MORTO) x = 0;
    if (abs(y) < PS4_RAIO_MORTO) y = 0;
    esq = limita(((y + x) * 100) / 127);
    dir = limita(((y - x) * 100) / 127);
    return x != 0 || y != 0;
  }

  // Quantos graus o servo anda em `dt_ms` com o manche direito em `v`
  // (-127 a 127). O manche move o servo em vez de apontar um angulo: no
  // fim, PS4_SERVO_GRAUS_POR_S; no raio morto, nada - e solto, o servo
  // fica onde parou.
  static float passoDoServo(int v, uint32_t dt_ms) {
    if (abs(v) < PS4_RAIO_MORTO) return 0.0f;
    return (float)v / 127.0f * PS4_SERVO_GRAUS_POR_S * (float)dt_ms / 1000.0f;
  }

  // O que a mao esta fazendo agora.
  struct Mao {
    int esq = 0, dir = 0;  // rodas, de -100 a 100
    bool mexeu = false;    // manche esquerdo fora do centro
    bool freio = false;    // O apertado
    bool foto  = false;    // X apertado
    int rx = 0, ry = 0;    // manche direito, de -127 a 127, y positivo para cima
    bool centra = false;   // R3 apertado
  };

#if FEIJAO_PS4_LIGADO
  // Sobe o Bluetooth com o proprio endereco desta placa - o que
  // `scripts/pareia_ps4.py` grava no controle.
  bool begin() {
    PS4.attach(&ControlePS4::aoRelatorio);
#ifdef FEIJAO_PS4_DEPURA
    // O log da biblioteca, compilado fora por padrao. Para ligar:
    //   build_flags = -DLOG_LOCAL_LEVEL=3 -DFEIJAO_PS4_DEPURA
    esp_log_level_set("PS4_L2CAP", ESP_LOG_INFO);
    esp_log_level_set("PS4_SPP", ESP_LOG_INFO);
#endif
    if (!PS4.begin()) return false;
    // A biblioteca nao olha o radio, so o que passa depois dele. Sem
    // isto, controle piscando sem conectar nao deixa rastro nenhum no
    // log - nem se chegou a bater na placa.
    esp_bt_gap_register_callback(&ControlePS4::aoRadio);
    // A chave tem que estar la ANTES de a placa atender: o controle a
    // pede no primeiro instante, e sem ela desiste em 0,35 s - chega,
    // pisca, sai, e nenhum canal chega a abrir. Visto na bancada em
    // 05/10/2026.
    tem_conhecido_ = leConhecido(conhecido_);
    if (tem_conhecido_) {
      // De tras para a frente: o controle guarda a chave na ordem em que
      // ela vai pelo radio, e a pilha guarda ao contrario e desvira ao
      // mandar (ARRAY16_TO_STREAM). Assim conectou na bancada.
      uint8_t chave[sizeof(CHAVE)];
      for (size_t i = 0; i < sizeof(chave); i++) chave[i] = CHAVE[sizeof(CHAVE) - 1 - i];
      BTA_DmAddDevice(conhecido_, nullptr, chave, 0, 1, CHAVE_COMBINADA, SEM_TECLADO_NEM_TELA, 0,
                      0);
    }
    // A biblioteca liga isto num evento do SPP; ligado aqui tambem, a
    // placa atende ao controle mesmo se esse evento nao vier.
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    return true;
  }

  String endereco() { return PS4.getAddress(); }
  bool conectado() { return PS4.isConnected(); }

  // O controle que a placa conhece, gravado por `lembra()`. Vazio se
  // nenhum: ai nenhum controle conecta.
  String conhecido() const {
    char s[18];
    return tem_conhecido_ ? String(texto(conhecido_, s)) : String();
  }

  // Guarda o endereco do controle pareado. Vale a partir do proximo
  // boot - quem chama reinicia a placa.
  static bool lembra(const char* mac) {
    uint8_t a[6];
    if (sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) !=
        6) {
      return false;
    }
    Preferences p;
    if (!p.begin(NVS_ESPACO, false)) return false;
    const bool ok = p.putBytes(NVS_CHAVE, a, sizeof(a)) == sizeof(a);
    p.end();
    return ok;
  }

  // Ha relatorio novo chegando? O controle manda dezenas por segundo
  // enquanto esta ligado; silencio aqui sem aviso de desconexao e
  // radio caindo - e quem cuida disso e o failsafe.
  bool falando() const { return (millis() - ultimo_ms_) < 200; }
  uint32_t relatorios() const { return relatorios_; }

  // Le o estado atual.
  Mao le() {
    Mao m;
    m.mexeu  = mistura(PS4.LStickX(), -PS4.LStickY(), m.esq, m.dir);
    m.freio  = PS4.Circle();
    m.foto   = PS4.Cross();
    m.rx     = PS4.RStickX();
    m.ry     = -PS4.RStickY();  // o controle conta para baixo
    m.centra = PS4.R3();
    return m;
  }

  // A barra de luz diz que o robo esta ouvindo o controle.
  void acende(uint8_t r, uint8_t g, uint8_t b) {
    PS4.setLed(r, g, b);
    PS4.sendToController();
  }
#else
  bool begin() { return false; }
  String endereco() { return String(); }
  bool conectado() { return false; }
  String conhecido() const { return String(); }
  static bool lembra(const char*) { return false; }
  bool falando() const { return false; }
  uint32_t relatorios() const { return 0; }
  Mao le() { return Mao(); }
  void acende(uint8_t, uint8_t, uint8_t) {}
#endif

private:
  static int limita(int v) {
    if (v > protocolo::VEL_MAX) return protocolo::VEL_MAX;
    if (v < protocolo::VEL_MIN) return protocolo::VEL_MIN;
    return v;
  }

  // Chamado pela task do Bluetooth, a cada relatorio do controle.
  static void aoRelatorio() {
    ultimo_ms_ = millis();
    relatorios_++;
  }

#if FEIJAO_PS4_LIGADO
  // Chamado pela task do Bluetooth, a cada evento do radio.
  static void aoRadio(esp_bt_gap_cb_event_t evento, esp_bt_gap_cb_param_t* p) {
    char mac[18];
    switch (evento) {
      case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        relatorios_na_chegada_ = relatorios_.load();
        Serial.printf("[ps4] radio: %s chegou (status %d)\n", texto(p->acl_conn_cmpl_stat.bda, mac),
                      p->acl_conn_cmpl_stat.stat);
        break;
      case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        Serial.printf("[ps4] radio: %s saiu (motivo 0x%02x)\n",
                      texto(p->acl_disconn_cmpl_stat.bda, mac), p->acl_disconn_cmpl_stat.reason);
        // Chegou e saiu sem mandar um relatorio: e o controle piscando sem
        // conectar. 0x105 e chave diferente (ele foi pareado com outra
        // coisa depois - o console, outro robo); 0x113 e esta placa nao
        // o conhecer. Os dois se resolvem pareando de novo.
        if (relatorios_ == relatorios_na_chegada_) {
          Serial.println("[ps4] ele so pisca? scripts/pareia_ps4.py --corpo <porta>");
        }
        break;
      case ESP_BT_GAP_AUTH_CMPL_EVT:
        Serial.printf("[ps4] radio: autenticacao com %s: status %d\n", texto(p->auth_cmpl.bda, mac),
                      p->auth_cmpl.stat);
        break;
      case ESP_BT_GAP_PIN_REQ_EVT:
        Serial.printf("[ps4] radio: %s pediu PIN\n", texto(p->pin_req.bda, mac));
        break;
      default: break;
    }
  }

  static bool leConhecido(uint8_t* a) {
    Preferences p;
    if (!p.begin(NVS_ESPACO, true)) return false;
    const bool ok = p.getBytes(NVS_CHAVE, a, 6) == 6;
    p.end();
    return ok;
  }

  // A chave de enlace que `scripts/pareia_ps4.py` grava no controle junto
  // com o endereco desta placa. Os dois lados TEM que ter a mesma: e com
  // ela que o controle confere, a cada conexao, que esta falando com o
  // "console" certo. Nao protege nada que importe - e um robo de sala -,
  // por isso e fixa e esta aqui.
  static constexpr uint8_t CHAVE[16]            = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                                   0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
  static constexpr uint8_t CHAVE_COMBINADA      = 0x00;  // tipo de chave do Bluetooth classico
  static constexpr uint8_t SEM_TECLADO_NEM_TELA = 0x03;  // BTM_IO_CAP_NONE
  static constexpr const char* NVS_ESPACO       = "ps4";
  static constexpr const char* NVS_CHAVE        = "controle";

  uint8_t conhecido_[6] = {0};
  bool tem_conhecido_   = false;

  static const char* texto(const uint8_t* a, char* s) {
    snprintf(s, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
    return s;
  }
#endif

  static inline std::atomic<uint32_t> ultimo_ms_{0};
  static inline std::atomic<uint32_t> relatorios_{0};
  static inline std::atomic<uint32_t> relatorios_na_chegada_{0};
};

}  // namespace corpo
