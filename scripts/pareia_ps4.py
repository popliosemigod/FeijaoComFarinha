#!/usr/bin/env python3
"""Pareia o controle de PS4 com o robô, pelo cabo USB do controle.

O DualShock 4 só se conecta ao endereço Bluetooth que ele guarda — o do console
com que foi pareado — e, a cada conexão, confere uma chave de enlace que os dois
lados têm que conhecer. É o que o PS4 combina com o controle pelo cabo, e o que
este script faz, sem instalar programa nenhum:

  1. grava no controle o endereço do corpo do robô (a ESP32 DevKit) e a chave;
  2. conta ao corpo, pelo USB dele, o endereço do controle. O corpo guarda e
     reinicia já com a chave na pilha do Bluetooth.

Uso, com o controle E o corpo ligados no USB do PC:

    python scripts/pareia_ps4.py --corpo COM4      # os dois lados de uma vez
    python scripts/pareia_ps4.py                   # só mostra o que o controle guarda

Depois: tire o cabo do controle e aperte o botão PS. A barra de luz fica verde
quando o robô aceita o controle.

Para voltar a usar o controle no console, basta ligá-lo no PS4 pelo cabo e
apertar PS: o console grava o endereço e a chave dele de novo.

Precisa de hidapi e pyserial:

    python -m pip install --user hidapi pyserial
"""
import argparse
import re
import sys
import time

SONY = 0x054C
DS4 = (0x05C4, 0x09CC, 0x0BA0)  # DS4 v1, DS4 v2, adaptador sem fio

# Os relatórios de configuração do DS4. Os endereços vêm de trás para a
# frente, byte menos significativo primeiro.
LER = 0x12    # 16 bytes: [1..6] endereço do controle, [10..15] o que ele guarda
GRAVAR = 0x13  # 23 bytes: [1..6] o endereço a guardar, [7..22] chave de enlace

# A chave de enlace. TEM que ser a mesma de `CHAVE` em src/controle_ps4.h:
# o controle a confere a cada conexão e, se o corpo não a conhece, chega,
# pisca e desiste em 0,35 s.
CHAVE = bytes(range(0x10, 0x20))

MAC = r"([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})"


def para_texto(invertido):
    return ":".join(f"{b:02x}" for b in reversed(invertido))


def de_texto(mac):
    partes = mac.strip().lower().split(":")
    if len(partes) != 6 or not all(re.fullmatch(r"[0-9a-f]{2}", p) for p in partes):
        raise ValueError(f"endereço inválido: {mac!r} (esperado aa:bb:cc:dd:ee:ff)")
    return bytes(int(p, 16) for p in reversed(partes))


def abre_controle():
    import hid

    for d in hid.enumerate(SONY):
        if d["product_id"] in DS4:
            h = hid.device()
            h.open_path(d["path"])
            return h, d["product_string"] or "DualShock 4"
    return None, None


def abre_corpo(porta):
    """Abre o USB do corpo reiniciando a placa, para o log começar do boot."""
    import serial

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = porta, 115200, 0.1
    s.dtr = s.rts = False
    s.open()
    s.rts = True  # EN no chão: a placa reinicia
    time.sleep(0.1)
    s.reset_input_buffer()
    s.rts = False
    return s


def espera(s, padrao, segundos=8):
    texto, fim = "", time.time() + segundos
    while time.time() < fim:
        texto += s.read(4096).decode(errors="replace")
        m = re.search(padrao, texto)
        if m:
            return m
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--corpo", metavar="PORTA", help="a porta USB do corpo (ex.: COM4)")
    args = ap.parse_args()

    corpo, alvo = None, None
    if args.corpo:
        corpo = abre_corpo(args.corpo)
        m = espera(corpo, r"Endereco desta placa: " + MAC)
        if not m:
            print(f"o corpo em {args.corpo} não anunciou o endereço no boot - "
                  "o firmware `corpo` está gravado nele?")
            return 1
        alvo = m.group(1).lower()
        print(f"corpo em {args.corpo}: {alvo}")

    h, nome = abre_controle()
    if h is None:
        print("nenhum controle de PS4 no USB - ligue o cabo e tente de novo")
        return 1

    try:
        info = bytes(h.get_feature_report(LER, 16))
        controle = para_texto(info[1:7])
        print(f"{nome}: {controle}")
        print(f"  pareado com: {para_texto(info[10:16])}")
        if alvo is None:
            return 0

        # Grava sempre, mesmo com o endereço já certo: a chave não dá para
        # ler de volta, e só assim se sabe que ela é a do robô.
        h.send_feature_report(bytes([GRAVAR]) + de_texto(alvo) + CHAVE)
    finally:
        h.close()

    # Depois de gravar o controle some do USB por um instante (OSError na
    # releitura, visto na bancada) - então reabre até ele voltar.
    agora, fim = None, time.time() + 5
    while agora is None and time.time() < fim:
        time.sleep(0.3)
        try:
            h, _ = abre_controle()
            if h:
                agora = para_texto(bytes(h.get_feature_report(LER, 16))[10:16])
                h.close()
        except OSError:
            pass
    if agora != alvo:
        print(f"  FALHOU: o controle continua com {agora}")
        return 1
    print(f"  agora pareado com: {agora} (e com a chave do robô)")

    corpo.write(f"PAREIA {controle}\n".encode())
    m = espera(corpo, r"esperando o controle " + MAC)
    corpo.close()
    if not m or m.group(1).lower() != controle:
        print("  FALHOU: o corpo não confirmou o controle - veja o console dele")
        return 1
    print(f"corpo: esperando o controle {controle}")
    print("\nTire o cabo do controle e aperte o botão PS. Verde na barra de luz = o robô aceitou.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
