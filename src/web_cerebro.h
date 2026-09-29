// =====================================================================
//  web_cerebro.h - a foto, servida para um telefone
//
//  Por que existe: Henrique decidiu usar sempre o celular (um Galaxy
//  A14) para testar os dois robôs, e o console de rede (console_rede.h)
//  é texto puro - telnet não mostra imagem. Este arquivo abre uma
//  segunda porta, a 80, só para foto: uma página com um botão "tirar
//  foto" e um botão "baixar", pensada para o navegador do telefone.
//
//  POR QUE BAIXAR E NÃO SÓ MOSTRAR. Uma <img> sozinha entrega a foto na
//  tela, mas não no álbum. O atributo `download` no link força o
//  Android a salvar o arquivo pela via normal de downloads do
//  navegador - e a galeria de fábrica da Samsung (a que o A14 traz)
//  indexa a pasta Download por padrão, sem app nem configuração extra.
//  Não é integração com a galeria: é fazer o navegador oferecer o
//  arquivo do jeito que a galeria já sabe encontrar sozinha.
//
//  UMA FOTO SÓ NA MEMÓRIA. O buffer mora em Camera (ver captura() em
//  camera.h) e é sobrescrito a cada pedido novo - mesmo desenho que o
//  FarmIO já usa do lado da câmera dele (main_cam.cpp, g_cop): não dá
//  para guardar histórico na placa, só a mais recente.
//
//  SEM SENHA, pela mesma razão do console_rede.h: é a rede de casa, e
//  quem já entrou nela já controla o robô por outras portas.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "camera.h"

namespace cerebro {

static const char PAGINA_WEB_CEREBRO[] PROGMEM =
    R"HTML(<!doctype html><html lang=pt-BR><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Feijao com Farinha</title><style>
:root{--verde:#3ddc84;--fundo:#10150f;--carta:#1a211a;--txt:#e8f0e4}
*{box-sizing:border-box}body{margin:0;background:var(--fundo);color:var(--txt);
font-family:ui-rounded,'Segoe UI',system-ui,sans-serif;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:20px;margin:4px 0 2px}h1 span{color:var(--verde)}
.sub{color:#8fa088;font-size:12px;margin-bottom:16px}
.carta{background:var(--carta);border-radius:14px;padding:14px;border:1px solid #263026}
img.foto{width:100%;border-radius:10px;margin-top:10px;display:block;background:#000;min-height:120px}
.linha{display:flex;gap:8px;margin-top:12px}
.bt{flex:1;padding:13px;border-radius:10px;border:1px solid #2f6a41;background:#173a22;
color:var(--txt);font:inherit;font-size:15px;font-weight:600;text-align:center;text-decoration:none;cursor:pointer}
.bt:disabled{opacity:.5}
.nota{font-size:12px;color:#8fa088;margin-top:8px}
</style>
<h1>Feijao com <span>Farinha</span></h1>
<div class=sub id=sub>tocando o botao, a camera tira uma foto nova</div>
<div class=carta>
<img class=foto id=foto hidden>
<div id=vazio style="text-align:center;color:#8fa088;padding:24px 0">nenhuma foto ainda</div>
<div class=linha>
<button class=bt id=btnFoto>Tirar foto</button>
<a class=bt id=btnBaixar href=# download="feijao.jpg" hidden>Baixar</a>
</div>
<div class=nota>"Baixar" salva no telefone; a galeria de fabrica costuma achar sozinha a pasta Download.
Tambem da para tocar e segurar a foto e escolher salvar.</div>
</div>
<script>
const $=id=>document.getElementById(id);
$('btnFoto').onclick=async()=>{
 const b=$('btnFoto');b.disabled=true;b.textContent='capturando...';
 try{
  const r=await fetch('/foto',{method:'POST'});
  const d=await r.json();
  if(!d.ok){$('sub').textContent='a camera nao respondeu';b.disabled=false;b.textContent='Tirar foto';return;}
  const url='/foto.jpg?t='+Date.now();
  $('foto').src=url;$('foto').hidden=false;$('vazio').hidden=true;
  $('btnBaixar').href=url;
  $('btnBaixar').download='feijao-'+Date.now()+'.jpg';
  $('btnBaixar').hidden=false;
  $('sub').textContent=d.largura+'x'+d.altura;
 }catch(e){$('sub').textContent='o robo nao respondeu';}
 b.disabled=false;b.textContent='Tirar foto';
};
</script></html>)HTML";

class WebCerebro {
public:
  void begin(Camera* camera) {
    camera_ = camera;
    servidor_.on("/", HTTP_GET, [this]() { pagina(); });
    servidor_.on("/foto", HTTP_POST, [this]() { pedeFoto(); });
    servidor_.on("/foto.jpg", HTTP_GET, [this]() { serveFoto(); });
    servidor_.onNotFound([this]() { servidor_.send(404, "text/plain", "nao existe"); });
    servidor_.begin();
    ligado_ = true;
  }

  // Chamar todo loop() - so entra em jogo se Wi-Fi subiu, igual ao
  // console_rede.h.
  void tick() {
    if (ligado_) servidor_.handleClient();
  }

  bool ligado() const { return ligado_; }

private:
  void pagina() { servidor_.send_P(200, "text/html; charset=utf-8", PAGINA_WEB_CEREBRO); }

  void pedeFoto() {
    if (camera_ == nullptr || !camera_->captura()) {
      servidor_.send(503, "application/json", "{\"ok\":false}");
      return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"largura\":%u,\"altura\":%u}",
             (unsigned)camera_->largura(), (unsigned)camera_->altura());
    servidor_.send(200, "application/json", buf);
  }

  // Binario direto do buffer: sem copiar para String, que dobraria o
  // pico de memoria bem na hora em que a foto acabou de chegar - a
  // mesma razao que o FarmIO ja documenta em web.h.
  void serveFoto() {
    if (camera_ == nullptr || camera_->tamanho() == 0) {
      servidor_.send(404, "text/plain", "nenhuma foto ainda - toque em Tirar foto");
      return;
    }
    servidor_.sendHeader("Cache-Control", "no-store");
    servidor_.setContentLength(camera_->tamanho());
    servidor_.send(200, "image/jpeg", "");
    servidor_.client().write(camera_->buffer(), camera_->tamanho());
  }

  Camera* camera_ = nullptr;
  WebServer servidor_{80};
  bool ligado_ = false;
};

}  // namespace cerebro
