// =====================================================================
//  console_rede.h - o mesmo console da serial, sem o cabo
//
//  Um cliente por vez, texto simples, um caractere por comando -
//  exatamente o console que ja existia na serial. A diferenca inteira
//  e o transporte: aqui e TCP puro na porta 23, entao qualquer
//  terminal serve:
//
//      telnet <ip-do-robo> 23
//      nc <ip-do-robo> 23
//
//  Nao ha protocolo novo para aprender nem software especial para
//  instalar - e a mesma decisao que fez o protocolo.h ser texto: as
//  duas pontas precisam ser depuraveis com um terminal e um teclado.
//
//  So aceita quando ha credencial de Wi-Fi (`WIFI_SSID` no
//  secrets.h). Sem rede, este arquivo nao faz nada - o robo continua
//  funcionando so pela serial, como sempre funcionou.
//
//  SEM SENHA NO PROTOCOLO. Isto e seguro pelo mesmo motivo que a ponte
//  do Jaspy roda em claro na LAN: e a rede de casa, nao a internet, e
//  quem entra na rede ja controla o robo pela mesma porta que o
//  cerebro usa para tudo o mais. Se este console sair da bancada para
//  um lugar com rede compartilhada, ele precisa de autenticacao antes
//  - ver a nota em `docs/03-protocolo-uart.md` sobre o mesmo assunto
//  no enlace serie.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>

namespace cerebro {

// A porta 23 e a do telnet classico - nenhuma ferramenta nova para
// abrir, e o numero que qualquer pessoa que ja usou um roteador
// reconhece de cabeca.
static const uint16_t ConsoleRedePorta = 23;

class ConsoleRede {
public:
  void begin() {
    servidor_.begin(ConsoleRedePorta);
    servidor_.setNoDelay(true);  // um caractere por vez: sem isso, atraso de ~40ms por tecla
    ligado_ = true;
  }

  // Chamado a cada volta do loop(). Aceita um cliente novo se nao
  // houver nenhum, esvazia o que o cliente atual mandou, e chama
  // `comando` para cada byte - exatamente como a serial faz.
  //
  // `comando` e um ponteiro de funcao, nao um metodo: e o que permite
  // o mesmo `executaComando(Print&, char)` do main_cerebro.cpp servir
  // aqui e na serial sem este arquivo conhecer nada do robo.
  void atende(void (*comando)(Print&, char)) {
    if (!ligado_) return;

    if (!cliente_ || !cliente_.connected()) {
      WiFiClient novo = servidor_.accept();
      if (novo) {
        // So um cliente por vez. Dois consoles mandando comando ao
        // mesmo robo ao mesmo tempo e pior que nenhum - ninguem sabe
        // quem esta no controle. O segundo que tentar entra e ve a
        // recusa, nao trava esperando.
        if (cliente_) {
          novo.println("ja ha um console conectado - tente de novo em instantes");
          novo.stop();
        } else {
          cliente_ = novo;
          cliente_.println("--- Feijao com Farinha: console de teste ---");
          cliente_.println("mesmos comandos do console serial - digite 'h' para a lista");
        }
      }
      return;
    }

    while (cliente_.available()) {
      comando(cliente_, (char)cliente_.read());
    }
  }

  // Para a linha "console rede" do `?`. Diz se ha alguem conectado -
  // e nao so se o servidor esta de pe - porque "ligado mas sozinho" e
  // um estado diferente de "alguem esta pilotando pela rede agora".
  //
  // Nao e `const`: `WiFiClient::connected()` nao e const na propria
  // biblioteca (ela consulta o socket ao vivo), entao fingir
  // constancia aqui so empurraria o problema para o chamador.
  String descricao() {
    if (!ligado_) return " desligado (sem Wi-Fi)";
    if (cliente_ && cliente_.connected()) {
      return " ligado, cliente conectado (" + cliente_.remoteIP().toString() + ")";
    }
    return " ligado, esperando em porta " + String(ConsoleRedePorta);
  }

private:
  WiFiServer servidor_{ConsoleRedePorta};
  WiFiClient cliente_;
  bool ligado_ = false;
};

}  // namespace cerebro
