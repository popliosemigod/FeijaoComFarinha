#!/usr/bin/env python3
"""Grava firmware novo na ESP32-CAM pelo enlace com o corpo, sem o adaptador.

A CAM so grava pelo UART0, e o adaptador que chega nele ocupa o header inteiro:
com ele encaixado, o microfone e o enlace ficam de fora. Este script usa o
caminho que continua ligado com o robo montado:

    PC --USB--> corpo (C3, em PONTE) --enlace--> CAM (atualiza_serial.h)

Uso:
    python scripts/grava_pelo_enlace.py                       # teste_som, COM7
    python scripts/grava_pelo_enlace.py --env cerebro_cam
    python scripts/grava_pelo_enlace.py --direto --porta COM6 # CAM no adaptador

`--direto` fala com o UART0 da CAM sem passar pelo corpo. Serve para provar o
receptor com a placa no adaptador, antes de depender dele com o robo montado.

So funciona se o firmware que JA ESTA na CAM tiver o receptor (teste_som de
30/09/2026 em diante). A primeira gravacao ainda e pelo adaptador.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import struct
import sys
import time
from pathlib import Path

import serial

RAIZ = Path(__file__).resolve().parents[1]
BLOCO = 512          # bytes por bloco; o receptor aceita ate 1024
FATIA = 64           # o USB do C3 e o UART a 115200 nao tem o mesmo folego:
PAUSA_FATIA = 0.006  # entregar em fatias evita afogar a ponte
TENTATIVAS = 12


def le_linha(porta: serial.Serial, prazo: float) -> str | None:
    """Uma linha de texto, ou None se o prazo passar."""
    fim = time.time() + prazo
    buf = bytearray()
    while time.time() < fim:
        b = porta.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode(errors="replace").strip()
        buf += b
    return None


def espera(porta: serial.Serial, prefixo: str, prazo: float, mostra: bool = True) -> str | None:
    """Le linhas ate uma comecar com `prefixo`. As outras sao log, e passam."""
    fim = time.time() + prazo
    while time.time() < fim:
        linha = le_linha(porta, max(0.05, fim - time.time()))
        if linha is None:
            return None
        if linha.startswith(prefixo):
            return linha
        if mostra and linha and not linha.startswith(("M ", "STOP")):
            print(f"    . {linha}")
    return None


def manda_bloco(porta: serial.Serial, offset: int, dados: bytes) -> None:
    corpo = struct.pack("<IH", offset, len(dados)) + dados
    quadro = b"\xA5" + corpo + struct.pack("<H", binascii.crc_hqx(corpo, 0xFFFF))
    for i in range(0, len(quadro), FATIA):
        porta.write(quadro[i:i + FATIA])
        porta.flush()
        time.sleep(PAUSA_FATIA)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--porta", default="COM7", help="porta do corpo (ou da CAM, com --direto)")
    ap.add_argument("--env", default="teste_som", help="ambiente do PlatformIO a gravar")
    ap.add_argument("--bin", type=Path, help="imagem a gravar (padrao: a do --env)")
    ap.add_argument("--direto", action="store_true", help="a porta e o UART0 da CAM, sem ponte")
    args = ap.parse_args()

    caminho = args.bin or RAIZ / ".pio" / "build" / args.env / "firmware.bin"
    if not caminho.exists():
        print(f"nao achei {caminho} - compile antes: pio run -e {args.env}")
        return 1
    imagem = caminho.read_bytes()
    md5 = hashlib.md5(imagem).hexdigest()
    print(f"{caminho.name}: {len(imagem)} bytes, md5 {md5}")

    porta = serial.Serial()
    porta.port, porta.baudrate, porta.timeout = args.porta, 115200, 0.05
    porta.dtr = porta.rts = False  # nao reiniciar a placa ao abrir
    porta.open()
    time.sleep(0.3)
    porta.reset_input_buffer()

    try:
        if not args.direto:
            porta.write(b"PONTE\n")
            if espera(porta, "[corpo] ponte aberta", 3) is None:
                print("o corpo nao abriu a ponte - o firmware dele tem o comando PONTE?")
                return 1
            print("ponte aberta, motores soltos")

        comando = f"U {len(imagem)} {md5}\n"
        porta.write(comando.encode() if args.direto else b">" + comando.encode())
        resposta = espera(porta, "#U", 10)
        if resposta != "#U pronto":
            print(f"a CAM nao aceitou a gravacao: {resposta!r}")
            print("o firmware que esta nela tem o receptor? (teste_som de 30/09 em diante)")
            return 1

        inicio = time.time()
        offset = 0
        while offset < len(imagem):
            dados = imagem[offset:offset + BLOCO]
            for tentativa in range(TENTATIVAS):
                manda_bloco(porta, offset, dados)
                resposta = espera(porta, "#U", 4, mostra=False)
                if resposta and resposta.startswith("#U k "):
                    offset = int(resposta.split()[2])
                    break
                if resposta and resposta.startswith("#U erro"):
                    print(f"\na CAM desistiu: {resposta}")
                    return 1
                print(f"\n  bloco em {offset} recusado ({resposta!r}), de novo [{tentativa + 1}]")
                time.sleep(0.2)
                porta.reset_input_buffer()
            else:
                print(f"\ndesisti no offset {offset}")
                return 1
            pct = 100 * offset // len(imagem)
            print(f"\r  {offset}/{len(imagem)} bytes ({pct}%)", end="", flush=True)

        print()
        resposta = espera(porta, "#U", 15)
        if resposta != "#U ok":
            print(f"a imagem chegou, mas a CAM nao confirmou: {resposta!r}")
            return 1
        print(f"gravado e conferido em {time.time() - inicio:.0f} s - a CAM esta reiniciando")
        return 0
    finally:
        porta.close()


if __name__ == "__main__":
    sys.exit(main())
