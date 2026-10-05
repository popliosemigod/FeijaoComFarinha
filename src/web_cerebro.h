// =====================================================================
//  web_cerebro.h - a pagina do robo: ver e dirigir pelo celular
//
//  Uma tela so, de proposito minima: a foto em cima, o direcional
//  embaixo. Nada de painel, grafico ou menu - quem abre isto quer ver o
//  que o robo ve e leva-lo ate ali.
//
//    foto       toca e a camera tira uma nova; "baixar" salva no telefone
//    direcional SEGURAR anda, SOLTAR para; o quadrado do meio freia
//
//  SEGURAR PARA ANDAR, e nao tocar e ele seguir, e a decisao que importa
//  aqui. Enquanto o dedo esta no botao, a pagina repete o comando a
//  cada 150 ms; se a repeticao parar - aba fechada, tela apagada, Wi-Fi
//  caindo -, o cerebro para o robo em PARADA_WEB_MS. Sem isso o
//  heartbeat do enlace continuaria mandando o ultimo movimento para
//  sempre, e o failsafe do corpo nunca dispararia: ele so ve o cerebro
//  vivo, nao o telefone morto.
//
//  POR QUE BAIXAR E NAO SO MOSTRAR. Uma <img> entrega a foto na tela,
//  nao no album. O atributo `download` faz o navegador salvar pela via
//  normal de downloads, e a galeria de fabrica do Android indexa a
//  pasta Download sozinha.
//
//  UMA FOTO SO NA MEMORIA: o buffer mora em Camera (ver captura() em
//  camera.h) e e sobrescrito a cada pedido.
//
//  Sem senha na pagina: quem entra na rede - a de casa, ou a propria do
//  robo, que tem senha - ja controla o robo pelas outras portas.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "camera.h"
#include "corpo_link.h"

namespace cerebro {

static const uint32_t PARADA_WEB_MS = 400;  // sem repeticao do telefone: para
static const int WEB_ANDA           = 50;   // % - o mesmo do `w` do console
static const int WEB_VIRA           = 45;   // % - o mesmo do `a` e do `d`

static const char PAGINA_WEB_CEREBRO[] PROGMEM =
    R"HTML(<!doctype html><html lang=pt-BR><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,user-scalable=no">
<title>Feijao com Farinha</title><style>
*{box-sizing:border-box}
body{margin:0 auto;max-width:420px;padding:16px;font:15px system-ui,sans-serif;
background:#111;color:#ddd;user-select:none;-webkit-user-select:none;touch-action:manipulation}
img{width:100%;aspect-ratio:4/3;object-fit:cover;background:#000;border-radius:12px;display:block}
p{margin:8px 0 20px;color:#777;font-size:13px;display:flex;justify-content:space-between}
a{color:#aaa}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;max-width:270px;margin:0 auto}
button{aspect-ratio:1;border:0;border-radius:14px;background:#222;color:#ddd;font-size:26px}
button:active,button.on{background:#3a3}
#s{background:#2a2a2a}
</style>
<img id=f alt="toque para tirar uma foto">
<p><span id=m>toque na imagem para tirar uma foto</span><a id=b download hidden>baixar</a></p>
<div class=pad>
<i></i><button data-v="50,50">&#9650;</button><i></i>
<button data-v="-45,45">&#9664;</button><button id=s data-v="0,0">&#9632;</button><button data-v="45,-45">&#9654;</button>
<i></i><button data-v="-50,-50">&#9660;</button><i></i>
</div>
<script>
const $=i=>document.getElementById(i);let t=0;
const ir=(u)=>fetch(u,{method:'POST'}).catch(()=>{$('m').textContent='o robo nao respondeu'});
const solta=b=>{clearInterval(t);t=0;b&&b.classList.remove('on');ir('/parar')};
document.querySelectorAll('button').forEach(b=>{
 const[e,d]=b.dataset.v.split(',');
 b.onpointerdown=ev=>{ev.preventDefault();solta();b.classList.add('on');
  if(b.id=='s')return;const u='/mover?e='+e+'&d='+d;ir(u);t=setInterval(()=>ir(u),150)};
 b.onpointerup=b.onpointerleave=b.onpointercancel=()=>{if(b.classList.contains('on'))solta(b)};
});
$('f').onclick=async()=>{$('m').textContent='tirando...';
 try{const r=await(await fetch('/foto',{method:'POST'})).json();
  if(!r.ok)throw 0;const u='/foto.jpg?t='+Date.now();
  $('f').src=u;$('b').href=u;$('b').download='feijao-'+Date.now()+'.jpg';$('b').hidden=false;
  $('m').textContent=r.largura+'x'+r.altura}catch(e){$('m').textContent='a camera nao respondeu'}};
document.onvisibilitychange=()=>{if(document.hidden&&t)solta()};
</script></html>)HTML";

class WebCerebro {
public:
  void begin(Camera* camera, Corpo* corpo) {
    camera_ = camera;
    corpo_  = corpo;
    servidor_.on("/", HTTP_GET, [this]() { pagina(); });
    servidor_.on("/foto", HTTP_POST, [this]() { pedeFoto(); });
    servidor_.on("/foto.jpg", HTTP_GET, [this]() { serveFoto(); });
    servidor_.on("/mover", HTTP_POST, [this]() { mover(); });
    servidor_.on("/parar", HTTP_POST, [this]() { parar(); });
    servidor_.onNotFound([this]() { servidor_.send(404, "text/plain", "nao existe"); });
    servidor_.begin();
    ligado_ = true;
  }

  // Chamar todo loop(). E tambem aqui que mora a parada por silencio
  // do telefone - ver o comeco do arquivo.
  void tick() {
    if (!ligado_) return;
    servidor_.handleClient();
    if (dirigindo_ && millis() - ultimo_mover_ms_ > PARADA_WEB_MS) {
      corpo_->para();
      dirigindo_ = false;
    }
  }

  bool ligado() const { return ligado_; }

private:
  void pagina() { servidor_.send_P(200, "text/html; charset=utf-8", PAGINA_WEB_CEREBRO); }

  void mover() {
    if (corpo_ == nullptr) {
      servidor_.send(503, "text/plain", "sem corpo");
      return;
    }
    // A faixa e conferida de novo aqui: a pagina e codigo que roda no
    // telefone de qualquer um, e o corpo so recebe o que passou por
    // `montaMotor`, que tambem prende no limite.
    const int e = constrain(servidor_.arg("e").toInt(), -WEB_ANDA, WEB_ANDA);
    const int d = constrain(servidor_.arg("d").toInt(), -WEB_ANDA, WEB_ANDA);
    corpo_->anda(e, d);
    ultimo_mover_ms_ = millis();
    dirigindo_       = true;
    servidor_.send(204);
  }

  void parar() {
    if (corpo_ != nullptr) corpo_->para();
    dirigindo_ = false;
    servidor_.send(204);
  }

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
  // pico de memoria bem na hora em que a foto acabou de chegar.
  void serveFoto() {
    if (camera_ == nullptr || camera_->tamanho() == 0) {
      servidor_.send(404, "text/plain", "nenhuma foto ainda");
      return;
    }
    servidor_.sendHeader("Cache-Control", "no-store");
    servidor_.setContentLength(camera_->tamanho());
    servidor_.send(200, "image/jpeg", "");
    servidor_.client().write(camera_->buffer(), camera_->tamanho());
  }

  Camera* camera_ = nullptr;
  Corpo* corpo_   = nullptr;
  WebServer servidor_{80};
  bool ligado_              = false;
  bool dirigindo_           = false;
  uint32_t ultimo_mover_ms_ = 0;
};

}  // namespace cerebro
