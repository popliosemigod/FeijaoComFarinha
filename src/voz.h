// =====================================================================
//  voz.h - a interface da voz, sem o servico atras dela
//
//  ESTE ARQUIVO E DE PROPOSITO UMA CASCA. O prompt do projeto diz para
//  perguntar antes de escolher o servico de reconhecimento e sintese,
//  e a escolha muda tudo que ficaria aqui dentro: quem manda audio
//  bruto para um PC na LAN nao escreve o mesmo codigo de quem fala com
//  uma API na nuvem.
//
//  O que ja da para fixar sem a decisao e a FORMA do contrato, e e ela
//  que esta abaixo. Enquanto ninguem plugar um servico de verdade, o
//  robo usa `VozDeMentira`, que registra no console o que teria dito.
//  Isso mantem a orquestracao inteira testavel - o robo anda, ouve,
//  "responde" e para - sem nenhuma chave de API.
//
//  Os tres caminhos possiveis, com o que cada um custa:
//
//  1. A PONTE DO JASPY, na LAN (recomendado).
//     O laboratorio ja tem um servidor que recebe audio, transcreve
//     com Whisper na maquina e devolve texto e ordens - e o mesmo que
//     move o avatar em AR. O robo entraria como mais um corpo do
//     mesmo agente, que e o que "baseado no corpo do Jaspy" quer
//     dizer. Custo: so funciona em casa, na mesma rede.
//
//  2. API na nuvem, direto da placa.
//     Funciona em qualquer lugar com internet. Custo: TLS no ESP32
//     come RAM, a latencia depende da rede de fora, e cada frase e
//     dinheiro.
//
//  3. Reconhecimento local (ESP-SR).
//     So roda no S3, e so para palavras fixas ("para", "vem", "anda").
//     Nao substitui os outros dois; convive bem como palavra de
//     acordar, e e o unico que funciona com a internet caida.
//
//  Enquanto a decisao nao vem, nenhuma delas esta escrita - e isso e
//  melhor que escrever uma e ter que desfazer.
// =====================================================================
#pragma once

#include <Arduino.h>

#include "config_cerebro.h"

namespace cerebro {

// O que o cerebro entende depois de ouvir. Comeca pequeno de
// proposito: este enum e o contrato entre "alguem falou" e "o robo
// faz", e contrato pequeno e facil de honrar.
enum class Intencao : uint8_t {
  NENHUMA,
  ANDAR,
  PARAR,
  VIRAR,
  OLHAR,
  FALAR_ALGO
};

struct Entendido {
  Intencao intencao = Intencao::NENHUMA;
  int a             = 0;  // ANDAR: velocidade | VIRAR: graus
  char texto[96]    = {0};
};

// A interface. Quem implementar isto pluga o servico escolhido sem
// tocar em mais nada do firmware.
class ServicoDeVoz {
public:
  virtual ~ServicoDeVoz() {}

  virtual bool begin() = 0;

  // Empurra um bloco de PCM 16 bits do microfone. Chamado varias
  // vezes por segundo: nao pode bloquear.
  virtual void ouve(const int16_t* amostras, size_t quantas) = 0;

  // Ha alguma intencao pronta? Nao bloqueia; devolve false quase
  // sempre, que e o normal de um robo numa sala silenciosa.
  virtual bool pegaIntencao(Entendido& saida) = 0;

  // Pede que uma frase seja falada. Quem produz o audio e o servico;
  // quem toca e `Audio`.
  virtual void diz(const char* frase) = 0;

  virtual bool ligado() const = 0;
};

// ---------------------------------------------------------------------
//  A implementacao que existe hoje: nenhuma.
//
//  Ela nao finge entender. Mostra o nivel do microfone - o que prova
//  que a captura funciona - e escreve no console o que teria sido
//  dito. Um robo que responde frase pronta sem ter entendido nada e
//  pior que um robo calado: o calado nao mente sobre o que ele e.
// ---------------------------------------------------------------------
class VozDeMentira : public ServicoDeVoz {
public:
  bool begin() override {
    Serial.println("[voz] nenhum servico configurado - modo silencioso");
    Serial.println("[voz] ver docs/05-a-voz.md: a escolha ainda e do Henrique");
    return true;
  }

  void ouve(const int16_t* amostras, size_t quantas) override {
    if (quantas == 0) return;
    uint64_t soma = 0;
    for (size_t i = 0; i < quantas; i++) soma += (uint64_t)abs(amostras[i]);
    const float n = (float)(soma / quantas) / 32768.0f;
    if (n > pico_) pico_ = n;
    blocos_++;
  }

  bool pegaIntencao(Entendido&) override { return false; }

  void diz(const char* frase) override { Serial.printf("[voz] diria: \"%s\"\n", frase); }

  bool ligado() const override { return false; }

  // Para o log periodico: prova que o microfone esta vivo sem gravar
  // nada e sem mandar nada para lugar nenhum.
  float picoEZera() {
    const float p = pico_;
    pico_         = 0.0f;
    return p;
  }
  uint32_t blocos() const { return blocos_; }

private:
  float pico_      = 0.0f;
  uint32_t blocos_ = 0;
};

}  // namespace cerebro
