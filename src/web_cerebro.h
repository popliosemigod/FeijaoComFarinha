// =====================================================================
//  web_cerebro.h - a pagina do robo: ver e dirigir pelo celular
//
//  Uma tela so, de proposito minima: a foto em cima, as ultimas fotos
//  numa fileira, o direcional embaixo. Nada de painel, grafico ou menu -
//  quem abre isto quer ver o que o robo ve e leva-lo ate ali.
//
//    foto       toca e a camera tira uma nova; "baixar" salva no telefone
//    fileira    as ultimas fotos (do X do controle, da pagina, do console);
//               tocar numa a mostra em cima. Atualiza sozinha
//    ouvido     as ultimas frases que o robo ouviu, e a ordem que tirou
//               de cada uma (voz_comandos.h)
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
//  OS OLHOS DO JASPY. A pagina e so a olhada rapida; o mesmo endereco
//  serve a quem quiser ver pelo robo:
//
//    GET  /foto.jpg        a foto mais recente
//    GET  /foto.jpg?n=12   uma foto pelo numero, enquanto estiver guardada
//    GET  /fotos           {"b":<boot>,"fotos":[12,11,...]}, a mais nova antes
//    POST /foto            tira uma agora: {"ok":true,"n":13,...}
//    GET  /voz             {"n":<conta>,"ouvido":[["frase","ordem"],...]}
//
//  As fotos moram em Camera (ver captura() em camera.h): as ultimas
//  Camera::GUARDADAS, numa fila que apaga a mais velha.
//
//  Sem senha na pagina, e a rede propria do robo tambem e aberta, a
//  pedido do Henrique: o celular entra e dirige. Ver config_cerebro_xiao.h
//  para fechar.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "camera.h"
#include "corpo_link.h"
#include "voz_comandos.h"

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
p{margin:8px 0;color:#777;font-size:13px;display:flex;justify-content:space-between}
a{color:#aaa}
#g{display:flex;gap:6px;overflow-x:auto;margin-bottom:20px;scrollbar-width:none}
#g img{width:22%;flex:none;border-radius:8px;opacity:.5}
#g img.on{opacity:1}
#v{margin:-8px 0 18px;color:#666;font-size:13px}#v div:first-child{color:#bbb}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;max-width:270px;margin:0 auto}
button{aspect-ratio:1;border:0;border-radius:14px;background:#222;color:#ddd;font-size:26px}
button:active,button.on{background:#3a3}
#s{background:#2a2a2a}
</style>
<img id=f alt="toque para tirar uma foto">
<p><span id=m>toque na imagem para tirar uma foto</span><a id=b download hidden>baixar</a></p>
<div id=g></div>
<div id=v></div>
<div class=pad>
<i></i><button data-v="50,50">&#9650;</button><i></i>
<button data-v="-45,45">&#9664;</button><button id=s data-v="0,0">&#9632;</button><button data-v="45,-45">&#9654;</button>
<i></i><button data-v="-50,-50">&#9660;</button><i></i>
</div>
<script>
const $=i=>document.getElementById(i);let t=0,ult=0,boot=0,ouv=-1;
const ir=(u)=>fetch(u,{method:'POST'}).catch(()=>{$('m').textContent='o robo nao respondeu'});
const solta=b=>{clearInterval(t);t=0;b&&b.classList.remove('on');ir('/parar')};
document.querySelectorAll('button').forEach(b=>{
 const[e,d]=b.dataset.v.split(',');
 b.onpointerdown=ev=>{ev.preventDefault();solta();b.classList.add('on');
  if(b.id=='s')return;const u='/mover?e='+e+'&d='+d;ir(u);t=setInterval(()=>ir(u),150)};
 b.onpointerup=b.onpointerleave=b.onpointercancel=()=>{if(b.classList.contains('on'))solta(b)};
});
const url=n=>'/foto.jpg?n='+n+'&b='+boot;
const mostra=n=>{const u=url(n);$('f').src=u;$('b').href=u;$('b').download='feijao-'+n+'.jpg';
 $('b').hidden=false;for(const i of $('g').children)i.classList.toggle('on',i.dataset.n==n)};
const lista=async()=>{try{const r=await(await fetch('/fotos')).json();boot=r.b;
 if(!r.fotos.length||r.fotos[0]==ult)return;ult=r.fotos[0];
 $('g').innerHTML=r.fotos.map(n=>'<img data-n='+n+' src="'+url(n)+'">').join('');
 mostra(ult);$('m').textContent='foto '+ult}catch(e){}};
$('g').onclick=e=>{const n=e.target.dataset.n;if(n){mostra(n);$('m').textContent='foto '+n}};
$('f').onclick=async()=>{$('m').textContent='tirando...';
 try{const r=await(await fetch('/foto',{method:'POST'})).json();if(!r.ok)throw 0;await lista()}
 catch(e){$('m').textContent='a camera nao respondeu'}};
const esc=s=>s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'})[c]);
const voz=async()=>{try{const r=await(await fetch('/voz')).json();if(r.n==ouv)return;ouv=r.n;
 $('v').innerHTML=r.ouvido.map(([f,o])=>'<div>\u201c'+esc(f)+'\u201d'+(o!='nada'?' &rarr; '+o:'')+'</div>').join('')}catch(e){}};
lista();voz();setInterval(()=>{if(!document.hidden)lista()},3000);
setInterval(()=>{if(!document.hidden)voz()},1000);
document.onvisibilitychange=()=>{if(document.hidden&&t)solta()};
</script></html>)HTML";

class WebCerebro {
public:
  void begin(Camera* camera, Corpo* corpo, const Ouvidos* ouvidos) {
    camera_  = camera;
    corpo_   = corpo;
    ouvidos_ = ouvidos;
    // Muda a cada boot: a numeracao das fotos recomeca do 1, e sem isto
    // o telefone mostraria do cache a foto 1 do boot anterior.
    boot_ = esp_random() & 0xffffff;
    servidor_.on("/", HTTP_GET, [this]() { pagina(); });
    servidor_.on("/foto", HTTP_POST, [this]() { pedeFoto(); });
    servidor_.on("/foto.jpg", HTTP_GET, [this]() { serveFoto(); });
    servidor_.on("/fotos", HTTP_GET, [this]() { listaFotos(); });
    servidor_.on("/voz", HTTP_GET, [this]() { listaOuvidos(); });
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
    const Camera::Foto* f = camera_->ultima();
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"n\":%lu,\"largura\":%u,\"altura\":%u}",
             (unsigned long)f->numero, (unsigned)f->largura, (unsigned)f->altura);
    servidor_.send(200, "application/json", buf);
  }

  void listaFotos() {
    uint32_t n[Camera::GUARDADAS];
    const uint8_t quantas = camera_ != nullptr ? camera_->numeros(n) : 0;
    String json           = "{\"b\":" + String(boot_) + ",\"fotos\":[";
    for (uint8_t i = 0; i < quantas; i++) {
      if (i > 0) json += ',';
      json += String(n[i]);
    }
    json += "]}";
    servidor_.sendHeader("Cache-Control", "no-store");
    servidor_.send(200, "application/json", json);
  }

  // As ultimas frases ouvidas, a mais nova antes. `n` muda a cada frase:
  // a pagina so redesenha quando ele muda.
  void listaOuvidos() {
    String json = "{\"n\":" + String(ouvidos_ ? ouvidos_->contador() : 0) + ",\"ouvido\":[";
    const uint8_t quantos = ouvidos_ ? ouvidos_->quantos() : 0;
    for (uint8_t i = 0; i < quantos; i++) {
      const Ouvidos::Ouvido& o = ouvidos_->ouvido(i);
      if (i > 0) json += ',';
      json += "[\"";
      // Aspas, barras e controle escapados; o resto do UTF-8 passa cru.
      for (const char* c = o.texto; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') {
          json += '\\';
          json += *c;
        } else if ((unsigned char)*c >= 0x20) {
          json += *c;
        }
      }
      json += "\",\"";
      json += nomeDaOrdem(o.ordem);
      json += "\"]";
    }
    json += "]}";
    servidor_.sendHeader("Cache-Control", "no-store");
    servidor_.send(200, "application/json; charset=utf-8", json);
  }

  // Binario direto do buffer: sem copiar para String, que dobraria o
  // pico de memoria bem na hora em que a foto acabou de chegar.
  //
  // Sem `n`, a mais recente, sempre fresca. Com `n`, aquela foto - que
  // nunca muda, entao o telefone pode guarda-la: a fileira so baixa a
  // foto nova, e nao as oito de novo a cada atualizacao.
  void serveFoto() {
    const bool pelo_numero = servidor_.hasArg("n");
    const Camera::Foto* f  = camera_ == nullptr ? nullptr
                             : pelo_numero      ? camera_->foto(servidor_.arg("n").toInt())
                                                : camera_->ultima();
    if (f == nullptr) {
      servidor_.send(404, "text/plain", "foto nao guardada");
      return;
    }
    servidor_.sendHeader("Cache-Control", pelo_numero ? "max-age=86400" : "no-store");
    servidor_.setContentLength(f->tamanho);
    servidor_.send(200, "image/jpeg", "");
    servidor_.client().write(f->buf, f->tamanho);
  }

  Camera* camera_         = nullptr;
  Corpo* corpo_           = nullptr;
  const Ouvidos* ouvidos_ = nullptr;
  WebServer servidor_{80};
  bool ligado_              = false;
  bool dirigindo_           = false;
  uint32_t ultimo_mover_ms_ = 0;
  uint32_t boot_            = 0;
};

}  // namespace cerebro
