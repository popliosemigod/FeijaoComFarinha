#!/usr/bin/env python3
"""Pareia o controle de PS4 com o robô, pelo cabo USB do controle.

O DualShock 4 só se conecta ao endereço Bluetooth que ele guarda — o do console
com que foi pareado. Este script lê esse endereço e, se pedido, grava no lugar
dele o endereço do corpo do robô (a ESP32 DevKit). É o que o SixaxisPairTool
faz, sem instalar programa nenhum.

Uso, com o controle ligado no USB do PC:

    python scripts/pareia_ps4.py                   # mostra o que o controle guarda
    python scripts/pareia_ps4.py --corpo COM4      # lê o endereço do corpo e grava no controle
    python scripts/pareia_ps4.py --grava 3c:8a:1f:77:01:76

Depois de gravar: tire o cabo e aperte o botão PS. A barra de luz fica verde
quando o robô aceita o controle.

Para voltar a usar o controle no console, basta ligá-lo no PS4 pelo cabo e
apertar PS: o console grava o endereço dele de novo.

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

# A chave de enlace é obrigatória no relatório, e a biblioteca do robô não a
# usa. Qualquer valor fixo serve.
CHAVE = bytes(range(0x10, 0x20))


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


def endereco_do_corpo(porta):
    """Reinicia o corpo e lê do boot o endereço Bluetooth que ele anunciou."""
    import serial

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = porta, 115200, 0.1
    s.dtr = s.rts = False
    s.open()
    s.rts = True  # EN no chão: a placa reinicia
    time.sleep(0.1)
    s.reset_input_buffer()
    s.rts = False
    texto, fim = "", time.time() + 8
    while time.time() < fim:
        texto += s.read(4096).decode(errors="replace")
        m = re.search(r"Endereco desta placa: ([0-9a-fA-F:]{17})", texto)
        if m:
            s.close()
            return m.group(1).lower()
    s.close()
    raise RuntimeError(f"o corpo em {porta} não anunciou o endereço no boot - "
                       "o firmware `corpo` está gravado nele?")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--corpo", metavar="PORTA", help="lê o endereço do corpo nesta porta e grava")
    ap.add_argument("--grava", metavar="MAC", help="grava este endereço no controle")
    args = ap.parse_args()

    alvo = None
    if args.corpo:
        alvo = endereco_do_corpo(args.corpo)
        print(f"endereço do corpo em {args.corpo}: {alvo}")
    elif args.grava:
        alvo = args.grava.lower()

    h, nome = abre_controle()
    if h is None:
        print("nenhum controle de PS4 no USB - ligue o cabo e tente de novo")
        return 1

    try:
        info = bytes(h.get_feature_report(LER, 16))
        print(f"{nome}: {para_texto(info[1:7])}")
        print(f"  pareado com: {para_texto(info[10:16])}")

        if alvo is None:
            return 0
        if para_texto(info[10:16]) == alvo:
            print("  já está pareado com o robô - nada a fazer")
            return 0

        h.send_feature_report(bytes([GRAVAR]) + de_texto(alvo) + CHAVE)
        time.sleep(0.2)
        agora = para_texto(bytes(h.get_feature_report(LER, 16))[10:16])
        if agora != alvo:
            print(f"  FALHOU: o controle continua com {agora}")
            return 1
        print(f"  agora pareado com: {agora}")
        print("\nTire o cabo e aperte o botão PS. Verde na barra de luz = o robô aceitou.")
        return 0
    finally:
        h.close()


if __name__ == "__main__":
    sys.exit(main())
