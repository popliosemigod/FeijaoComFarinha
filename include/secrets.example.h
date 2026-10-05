// =====================================================================
//  secrets.example.h - o modelo, sem nenhum valor real
//
//  COPIE para `include/secrets.h` e preencha. O arquivo `secrets.h`
//  esta no .gitignore e nunca deve ser versionado.
//
//      cp include/secrets.example.h include/secrets.h
//
//  E O FIRMWARE PRECISA COMPILAR SEM ELE. Isso nao e detalhe de
//  organizacao: e o que permite o CI compilar o repositorio inteiro
//  sem ter senha nenhuma, e o que faz o robo subir e andar mesmo com
//  a rede indisponivel. Sem este arquivo, `config_cerebro.h` cai nos
//  valores vazios e o cerebro anuncia no log que esta sem rede.
// =====================================================================
#pragma once

// ---- Wi-Fi ----------------------------------------------------------
// Rede 2,4 GHz. O radio do ESP32 nao ve 5 GHz, e o sintoma de apontar
// para uma rede de 5 GHz e "rede nao encontrada" - nunca "senha
// errada". O diagnostico se perde procurando o lugar errado.
//
// Preencher isto TAMBEM liga o console de teste por rede (porta 23,
// telnet), com os mesmos comandos do console serial. Ver
// docs/06-montagem-do-corpo.md - "O mesmo console, sem cabo".
#define WIFI_SSID  "minha-rede-24ghz"
#define WIFI_SENHA "a-senha"

// Opcional: a senha da rede propria do robo (8 caracteres ou mais),
// usada quando a rede de cima nao responde. Sem ela, a senha sai do
// endereco da placa e aparece no console no boot.
// #define AP_SENHA "uma-senha-sua"

// Opcional, so no corpo: o endereco Bluetooth que o controle de PS4 ja
// guarda. Sem ele, o corpo usa o proprio e `scripts/pareia_ps4.py`
// grava esse no controle.
// #define PS4_MAC "aa:bb:cc:dd:ee:ff"

// ---- Servico de voz -------------------------------------------------
// AINDA NAO DECIDIDO - ver docs/05-a-voz.md. Quando for, este e o
// lugar do endereco e da credencial.
//
// #define VOZ_SERVIDOR "192.168.0.10:8081"
// #define VOZ_TOKEN    "..."
