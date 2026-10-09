// =====================================================================
//  main_cerebro.cpp - XIAO ESP32-S3 Sense: o cerebro
//
//  Ele cuida de camera, microfone, alto-falante e decisao, e manda
//  movimento para o corpo pela UART. Nunca aciona motor direto.
//
//  Tres tarefas, cada uma no seu tempo, e nenhuma esperando a outra:
//
//    corpo-link   (nucleo 0, prio 3)  reenvia movimento a cada 250 ms
//    sentidos     (nucleo 1, prio 2)  le o microfone e alimenta a voz
//    loop()       (nucleo 1, prio 1)  decide, registra, atende console
//
//  A separacao nao e enfeite de arquitetura: e o que garante que uma
//  chamada de rede lenta na decisao nao atrase o heartbeat e nao faca
//  o robo parar no meio da sala.
//
//  O ROBO NAO ANDA SOZINHO AO LIGAR, e isso e decisao de seguranca.
//  A rotina de teste so comeca quando alguem digita `t` no console -
//  a placa costuma ser ligada com o robo na mesa, e uma mesa tem
//  borda. Ate la ele fica parado, mandando `M 0 0`, que e diferente
//  de ficar mudo.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>

#include "audio.h"
#include "camera.h"
#include "config_cerebro.h"
#include "console_rede.h"
#include "corpo_link.h"
#include "linha.h"
#include "microfone.h"
#include "protocolo.h"
#include "voz.h"
#include "voz_comandos.h"
#include "voz_serial.h"
#include "web_cerebro.h"

namespace {

cerebro::Corpo corpo;
cerebro::Audio alto_falante;
cerebro::Microfone microfone;
cerebro::Camera camera;
cerebro::VozPorSerial voz;
cerebro::ConsoleRede console_rede;
cerebro::WebCerebro web_cerebro;
cerebro::Ouvidos ouvidos;

// Rede: a de casa (WIFI_SSID) ou, sem ela, a propria do robo.
String rede_ip;
bool rede_propria = false;

bool tem_audio  = false;
bool tem_mic    = false;
bool tem_camera = false;

// Alternancia das teclas 1 e 2 do console: cada toque manda o servo
// para o outro extremo, o que basta para ver se ele obedece.
bool servo1_alto = false;
bool servo2_alto = false;

// Teclas que chegam do corpo (`>p`, que o X do controle manda, ou `>?`
// digitado no USB de la). A task do enlace so as guarda aqui; quem
// executa e o loop(), como as do console - uma foto leva 1,5 s, e a
// task do enlace nao pode parar o heartbeat esse tempo todo.
QueueHandle_t do_corpo = nullptr;

void recebeDoCorpo(const char* texto) {
  for (const char* c = texto; *c != '\0'; c++) xQueueSend(do_corpo, c, 0);
}

// ---- Voz: o texto que o PC ouviu vira ordem ------------------------
//
//  O PC (`scripts/ouve.py`) devolve cada frase pelo USB numa linha
//  `!texto`; quem decide o que ela quer dizer e voz_comandos.h.
//
//  CADA ORDEM ANDA POR UM PRAZO E PARA SOZINHA (VOZ_ANDA_MS,
//  VOZ_VIRA_MS). A fala chega uns 3 s depois de dita - o Whisper leva
//  esse tempo -, entao um "pare" sempre chegaria tarde: quem para o
//  robo e o prazo. Tecla de movimento no console cancela o prazo.
bool voz_andando      = false;
uint32_t voz_desde_ms = 0;
uint32_t voz_prazo_ms = 0;

void obedeceVoz(const char* texto) {
  const cerebro::Ordem o = cerebro::entende(texto);
  ouvidos.anota(texto, o);
  Serial.printf("[voz] ouvi \"%s\" -> %s\n", texto, cerebro::nomeDaOrdem(o));

  switch (o) {
    case cerebro::Ordem::FRENTE: corpo.anda(cerebro::WEB_ANDA, cerebro::WEB_ANDA); break;
    case cerebro::Ordem::TRAS: corpo.anda(-cerebro::WEB_ANDA, -cerebro::WEB_ANDA); break;
    case cerebro::Ordem::ESQUERDA: corpo.anda(-cerebro::WEB_VIRA, cerebro::WEB_VIRA); break;
    case cerebro::Ordem::DIREITA: corpo.anda(cerebro::WEB_VIRA, -cerebro::WEB_VIRA); break;
    case cerebro::Ordem::PARA: corpo.para(); break;
    case cerebro::Ordem::NADA: return;  // conversa, nao ordem
  }
  voz_andando  = o != cerebro::Ordem::PARA;
  voz_desde_ms = millis();
  voz_prazo_ms =
      (o == cerebro::Ordem::FRENTE || o == cerebro::Ordem::TRAS) ? VOZ_ANDA_MS : VOZ_VIRA_MS;
  char relato[32];
  snprintf(relato, sizeof(relato), "voz: %s", cerebro::nomeDaOrdem(o));
  corpo.relata(relato);
}

// ---- Sentidos: o microfone, lido sem atrapalhar ninguem ------------
void tarefaSentidos(void*) {
  static int16_t bloco[256];
  uint32_t mudo = 0;  // leituras seguidas sem nenhuma amostra
  for (;;) {
    if (tem_mic) {
      const size_t n = microfone.le(bloco, sizeof(bloco) / sizeof(bloco[0]));
      if (n > 0) {
        if (mudo > 50) Serial.println("[sentidos] microfone voltou a dar sinal");
        mudo = 0;
        voz.ouve(bloco, n);
        voz.despacha(Serial);
        // Sem espera aqui, e isso e o que mantem o audio inteiro: a
        // leitura ja bloqueia ate o bloco chegar, entao a task dorme
        // sozinha. Com os 20 ms de baixo ela lia 16 ms de som a cada
        // 36 - bastava para medir nivel, e picotava qualquer frase.
        continue;
      } else {
        // Microfone sem nada ligado devolve timeout a cada leitura, e
        // o driver reclama sozinho no log - vinte vezes por segundo,
        // o que enterra qualquer outra mensagem. Depois de um segundo
        // assim, espaca as tentativas: o diagnostico continua, porque
        // ele volta a acusar quando o sinal voltar, e o console volta
        // a ser legivel.
        mudo++;
        if (mudo == 50) {
          Serial.println("[sentidos] microfone sem sinal - conferir a ligacao do INMP441");
        }
        if (mudo > 50) {
          vTaskDelay(pdMS_TO_TICKS(2000));
          continue;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- A rotina de teste do prompt -----------------------------------
//
//  Anda para a frente, para, e mexe os dois servos. E o menor teste
//  que prova o caminho inteiro: cerebro -> UART -> parser -> ponte H
//  e -> servo. Se ela roda, o enlace esta de pe.
//
//  Roda numa task propria e efemera para nao segurar o `loop()`: o
//  heartbeat continua correndo em paralelo, que e justamente o que se
//  quer exercitar.
void tarefaTeste(void*) {
  Serial.println("\n[teste] 1/4 - frente, 40%, por 1,5 s");
  corpo.anda(40, 40);
  vTaskDelay(pdMS_TO_TICKS(1500));

  Serial.println("[teste] 2/4 - parar");
  corpo.para();
  vTaskDelay(pdMS_TO_TICKS(800));

  Serial.println("[teste] 3/4 - girar no lugar, 1 s");
  corpo.anda(50, -50);
  vTaskDelay(pdMS_TO_TICKS(1000));
  corpo.para();
  vTaskDelay(pdMS_TO_TICKS(500));

  Serial.println("[teste] 4/4 - servos: 45, 135, 90");
  const int angulos[] = {45, 135, 90};
  for (int a : angulos) {
    corpo.servo(1, a);
    vTaskDelay(pdMS_TO_TICKS(400));
    corpo.servo(2, a);
    vTaskDelay(pdMS_TO_TICKS(400));
  }

  if (tem_audio) alto_falante.bipe(1200, 120);
  Serial.printf("[teste] fim. corpo %s, %lu enviados, %lu respostas, %lu erros\n",
                corpo.vivo() ? "respondendo" : "MUDO", (unsigned long)corpo.enviados(),
                (unsigned long)corpo.respostas(), (unsigned long)corpo.erros());
  vTaskDelete(nullptr);
}

void ajuda(Print& saida) {
  saida.println("\n--- console do cerebro (serial ou rede: e o mesmo) ---");
  saida.println("  t          rotina de teste (anda, para, mexe os servos)");
  saida.println("  w a s d x  frente, esquerda, re, direita, parar");
  saida.println("  1 / 2      servo 1 / servo 2 para 45, depois 135");
  saida.println("  b          bipe no alto-falante");
  saida.println("  f          foto: mede um quadro da camera");
  saida.println("  p          foto: tira, guarda para a pagina e despeja em base64");
  saida.println("  o / q      escuta: manda cada frase ouvida para o PC transcrever / para");
  saida.println("  !texto     o que o PC ouviu (scripts/ouve.py): frente, tras, esquerda,");
  saida.println("             direita, pare - anda um pouco e para sozinho");
  saida.println("  ?          estado de tudo (mostra a pagina e a rede)");
}

void estado(Print& saida) {
  saida.printf("\n=== %s v%s ===\n", CEREBRO_NOME, CEREBRO_VERSAO);
  saida.printf("corpo:       %s (%lu ms desde a ultima resposta)\n",
               corpo.vivo() ? "respondendo" : "MUDO", (unsigned long)corpo.desde_resposta_ms());
  saida.printf("             %lu enviados, %lu respostas, %lu recusas\n",
               (unsigned long)corpo.enviados(), (unsigned long)corpo.respostas(),
               (unsigned long)corpo.erros());
  // "FALHOU" e "nao existe nesta placa" sao coisas diferentes, e o
  // diagnostico so serve se disser qual das duas e. Numa placa sem
  // amplificador, "FALHOU" manda procurar defeito onde nao ha nada.
  saida.printf("alto-falante:%s\n",
               tem_audio ? " ok" : (TEM_AMPLIFICADOR ? " FALHOU" : " nao ha nesta placa"));
  saida.printf("microfone:   %s  pico recente %.3f em %lu blocos\n", tem_mic ? "ok" : "FALHOU",
               voz.picoEZera(), (unsigned long)voz.blocos());
  saida.printf("camera:      %s (sensor 0x%x)\n", tem_camera ? "ok" : "FALHOU",
               (unsigned)camera.sensor());
  uint32_t numeros[cerebro::Camera::GUARDADAS];
  const uint8_t guardadas = camera.numeros(numeros);
  saida.printf("fotos:       %u guardadas", (unsigned)guardadas);
  if (guardadas > 0) {
    saida.printf(", da %lu a %lu", (unsigned long)numeros[guardadas - 1],
                 (unsigned long)numeros[0]);
  }
  saida.println();
  saida.printf("voz:         escuta %s, %lu frases mandadas, fundo %.4f\n",
               voz.ligado() ? "LIGADA" : "desligada (`o` liga)", (unsigned long)voz.frases(),
               voz.fundo());
  if (ouvidos.quantos() > 0) {
    saida.printf("             ultima ouvida: \"%s\" -> %s\n", ouvidos.ouvido(0).texto,
                 cerebro::nomeDaOrdem(ouvidos.ouvido(0).ordem));
  }
  saida.printf("PSRAM:       %u KB livres de %u KB\n", (unsigned)(ESP.getFreePsram() / 1024),
               (unsigned)(ESP.getPsramSize() / 1024));
  saida.printf("heap:        %u KB livres\n", (unsigned)(ESP.getFreeHeap() / 1024));
  saida.printf("console rede:%s\n", console_rede.descricao().c_str());
  if (web_cerebro.ligado()) {
    saida.printf("pagina:      http://%s/\n", rede_ip.c_str());
    if (rede_propria) {
      saida.printf("rede:        \"%s\", %s\n", AP_NOME,
                   AP_SENHA[0] != '\0' ? "com a senha de secrets.h" : "aberta, sem senha");
    }
  } else {
    saida.println("pagina:      desligada (sem Wi-Fi)");
  }
}

// ---- O comando, vindo de onde vier ---------------------------------
//
//  Serial e Wi-Fi chamam a MESMA funcao. Nao ha um "modo remoto"
//  parecido com o console e um "modo local" de verdade - sao a mesma
//  coisa, e por isso nunca podem divergir silenciosamente. `saida` e
//  onde a resposta vai: o Serial do USB, ou o cliente TCP conectado.
void executaComando(Print& saida, char c) {
  switch (c) {
    case 't': xTaskCreatePinnedToCore(tarefaTeste, "teste", 4096, nullptr, 2, nullptr, 1); break;
    case 'w':
      voz_andando = false;
      corpo.anda(50, 50);
      saida.println("frente");
      break;
    case 's':
      voz_andando = false;
      corpo.anda(-50, -50);
      saida.println("re");
      break;
    case 'a':
      voz_andando = false;
      corpo.anda(-45, 45);
      saida.println("esquerda");
      break;
    case 'd':
      voz_andando = false;
      corpo.anda(45, -45);
      saida.println("direita");
      break;
    case 'x':
      voz_andando = false;
      corpo.para();
      saida.println("parar");
      break;
    case '1':
      corpo.servo(1, servo1_alto ? 45 : 135);
      servo1_alto = !servo1_alto;
      break;
    case '2':
      corpo.servo(2, servo2_alto ? 45 : 135);
      servo2_alto = !servo2_alto;
      break;
    case 'b':
      if (tem_audio) alto_falante.bipe();
      break;
    case 'p': {
      // Tira, guarda (e a foto que a pagina mostra) e despeja em base64.
      // Pela serial ou pela rede - as duas sao so um Print, e a funcao
      // nao sabe nem precisa saber qual. O corpo fica sabendo pelo
      // relato: e assim que o X do controle aparece no log de la.
      saida.println("[camera] capturando...");
      char relato[48];
      if (tem_camera && camera.despeja(saida)) {
        snprintf(relato, sizeof(relato), "foto %lu: %ux%u, %u bytes",
                 (unsigned long)camera.ultima()->numero, (unsigned)camera.largura(),
                 (unsigned)camera.altura(), (unsigned)camera.tamanho());
      } else {
        saida.println("[camera] falhou");
        snprintf(relato, sizeof(relato), "foto FALHOU");
      }
      corpo.relata(relato);
      break;
    }
    case 'f': {
      const size_t n = camera.mede();
      saida.printf("[camera] quadro de %u bytes\n", (unsigned)n);
      break;
    }
    case 'o':
      // O audio sai sempre pelo console da placa, mesmo que a tecla
      // tenha vindo pela rede: e la que `scripts/ouve.py` esta lendo.
      voz.escuta(true);
      saida.println("[voz] escuta ligada - cada frase ouvida sai em base64, para scripts/ouve.py");
      break;
    case 'q':
      voz.escuta(false);
      saida.println("[voz] escuta desligada");
      break;
    case '?': estado(saida); break;
    case 'h': ajuda(saida); break;
    default: break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t ate = millis() + 2000;
  while (!Serial && millis() < ate) delay(10);  // da tempo de o USB aparecer

  Serial.printf("\n=== %s v%s ===\n", CEREBRO_NOME, CEREBRO_VERSAO);

  // O enlace com o corpo PRIMEIRO. Se alguma coisa abaixo travar, o
  // corpo ja esta recebendo heartbeat e o robo esta parado de forma
  // deliberada, em vez de parado por failsafe.
  do_corpo = xQueueCreate(16, sizeof(char));
  corpo.aoConsole(recebeDoCorpo);
  if (!corpo.begin()) {
    Serial.println("[cerebro] FALHA ao subir o enlace com o corpo");
  } else {
    Serial.printf("[cerebro] enlace de pe: TX=%d RX=%d, heartbeat a cada %lu ms\n", PIN_UART_TX,
                  PIN_UART_RX, (unsigned long)protocolo::HEARTBEAT_MS);
  }

  // A CAMERA PRIMEIRO, E A ORDEM E O DEFEITO QUE ISTO CONSERTA.
  //
  // No ESP32 classico a camera NAO tem controlador proprio: a
  // interface DVP roda sobre o I2S0, em modo paralelo, e so o I2S0
  // serve. Subir o microfone antes fazia ele levar o I2S0 embora, e a
  // camera morria com uma mensagem que nao fala em camera nenhuma:
  //
  //     No free interrupt inputs for I2S0 interrupt
  //     Camera config failed with error 0xffffffff
  //
  // Com a camera primeiro, ela fica com o I2S0 e o microfone cai no
  // I2S1 sozinho. Na XIAO S3 a ordem e indiferente - la a camera tem
  // controlador proprio -, entao a mesma sequencia serve nas duas.
  //
  // Achado com a placa na mesa em 29/09/2026. Nenhuma leitura de
  // datasheet tinha apontado isso.
  tem_camera = camera.begin();
  Serial.printf("[cerebro] camera: %s", tem_camera ? "ok" : "FALHOU");
  if (!tem_camera) Serial.printf(" (erro 0x%x)", camera.erro());
  Serial.println();

  tem_audio = alto_falante.begin();
  Serial.printf("[cerebro] alto-falante: %s\n",
                tem_audio ? "ok" : (TEM_AMPLIFICADOR ? "FALHOU" : "nao ha nesta placa"));

  tem_mic = microfone.begin();
  Serial.printf("[cerebro] microfone: %s\n", tem_mic ? "ok" : "FALHOU");

  voz.begin();

  // ---- Wi-Fi: a rede de casa, ou a propria do robo -----------------
  //
  // Com WIFI_SSID em secrets.h, o robo entra na rede de casa. Sem ele -
  // ou se ela nao responder em 10 s -, o robo CRIA a propria rede, e o
  // celular entra nela. A pagina de dirigir nunca depende de alguem
  // ter configurado nada: e a mesma regra do resto do firmware, falta
  // de segredo nunca tira funcao do robo.
  //
  // A rede propria e ABERTA, a menos que secrets.h defina AP_SENHA: o
  // celular entra direto (pedido do Henrique, 08/10/2026). Quem esta ao
  // alcance ve a camera e dirige - ver config_cerebro_xiao.h.
  bool na_rede = false;
  if (WIFI_SSID[0] != '\0') {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_SENHA);
    Serial.printf("[cerebro] conectando a \"%s\"", WIFI_SSID);
    const uint32_t espera = millis() + 10000;
    while (WiFi.status() != WL_CONNECTED && millis() < espera) {
      delay(300);
      Serial.print('.');
    }
    Serial.println();
    na_rede = WiFi.status() == WL_CONNECTED;
    if (na_rede) rede_ip = WiFi.localIP().toString();
  }
  if (!na_rede) {
    WiFi.mode(WIFI_AP);
    // Senha vazia (nullptr) e rede aberta.
    na_rede      = WiFi.softAP(AP_NOME, AP_SENHA[0] != '\0' ? AP_SENHA : nullptr);
    rede_propria = na_rede;
    if (na_rede) rede_ip = WiFi.softAPIP().toString();
  }
  if (na_rede) {
    console_rede.begin();
    web_cerebro.begin(&camera, &corpo, &ouvidos);
    if (rede_propria) {
      Serial.printf("[cerebro] rede propria \"%s\", %s\n", AP_NOME,
                    AP_SENHA[0] != '\0' ? "com a senha de secrets.h" : "aberta, sem senha");
    }
    Serial.printf("[cerebro] pagina em http://%s/ - console em telnet %s %u\n", rede_ip.c_str(),
                  rede_ip.c_str(), (unsigned)cerebro::ConsoleRedePorta);
  } else {
    Serial.println("[cerebro] Wi-Fi nao subiu - seguindo so com a serial");
  }

  xTaskCreatePinnedToCore(tarefaSentidos, "sentidos", 6144, nullptr, 2, nullptr, 1);

  if (tem_audio) alto_falante.bipe(660, 90);
  ajuda(Serial);
}

void loop() {
  // ---- Console pela serial ----------------------------------------
  // Tecla solta e comando de uma letra. Linha que comeca com '!' e o
  // texto que o PC ouviu: vai inteira para a voz. Linha que nao fecha em
  // meio segundo e descartada, para nao engolir as teclas seguintes.
  static char frase[128];
  static size_t na_frase      = 0;
  static bool lendo_frase     = false;
  static uint32_t frase_desde = 0;
  if (lendo_frase && millis() - frase_desde > 500) lendo_frase = false;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (lendo_frase) {
      if (c == '\n' || c == '\r') {
        frase[na_frase] = '\0';
        lendo_frase     = false;
        obedeceVoz(frase);
      } else if (na_frase < sizeof(frase) - 1) {
        frase[na_frase++] = c;
      }
    } else if (c == '!') {
      lendo_frase = true;
      na_frase    = 0;
      frase_desde = millis();
    } else {
      executaComando(Serial, c);
    }
  }

  // ---- O prazo da ultima ordem de voz -------------------------------
  if (voz_andando && millis() - voz_desde_ms >= voz_prazo_ms) {
    corpo.para();
    voz_andando = false;
  }

  // ---- Console vindo do corpo ---------------------------------------
  // A resposta sai no USB daqui: e la que `scripts/fotos.py` espera a
  // foto. O corpo recebe so o relato curto.
  char c;
  while (do_corpo != nullptr && xQueueReceive(do_corpo, &c, 0) == pdTRUE) {
    executaComando(Serial, c);
  }

  // ---- Console pela rede -------------------------------------------
  // Mesmo comando, mesma funcao - ver a nota em cima de
  // executaComando(). So entra em jogo se o Wi-Fi subiu.
  console_rede.atende(executaComando);

  // ---- Foto pelo navegador do celular -------------------------------
  // Segunda porta, so para imagem - o console de rede e texto e nao
  // mostra foto nenhuma. So entra em jogo se o Wi-Fi subiu.
  web_cerebro.tick();

  // ---- Um sinal de vida, sem inundar o console -------------------
  static uint32_t ultimo_relato = 0;
  if (millis() - ultimo_relato > 10000) {
    ultimo_relato = millis();
    if (!corpo.vivo()) {
      Serial.printf("[cerebro] corpo MUDO ha %lu ms - conferir o cabo da UART\n",
                    (unsigned long)corpo.desde_resposta_ms());
    }
  }

  delay(10);
}
