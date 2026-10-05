#!/usr/bin/env python3
"""Salva no PC cada foto que o cérebro (a XIAO) solta no USB dele.

O X do controle de PS4 pede a foto: o corpo repassa o pedido ao cérebro, que
tira, guarda para a página do celular e despeja em base64 no USB. Este script
fica ouvindo e grava cada uma em evidencias/fotos/ (fora do git). A tecla `p`
no console da XIAO dá no mesmo.

    python scripts/fotos.py --porta COM13

Ctrl+C para sair. A porta não pode estar aberta em outro programa (monitor do
PlatformIO, scripts/ouve.py).
"""
import argparse
import base64
import re
import sys
import time
from pathlib import Path

import serial

RAIZ = Path(__file__).resolve().parent.parent
PASTA_SAIDA = RAIZ / "evidencias" / "fotos"
INICIO = re.compile(r"---FOTO-INICIO (\d+) (\d+) (\d+)---")
FIM = "---FOTO-FIM---"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--porta", default="COM13", help="porta USB do cérebro")
    ap.add_argument("--segundos", type=float, default=0, help="para sozinho depois disso (0 = nunca)")
    ap.add_argument("--agora", action="store_true", help="pede uma foto ao começar, sem o controle")
    args = ap.parse_args()

    PASTA_SAIDA.mkdir(parents=True, exist_ok=True)
    porta = serial.Serial()
    porta.port, porta.baudrate, porta.timeout = args.porta, 115200, 0.1
    porta.dtr = porta.rts = False  # não reiniciar a placa ao abrir
    porta.open()
    print(f"esperando fotos em {args.porta} - aperte X no controle (Ctrl+C sai)")
    if args.agora:
        porta.write(b"p")

    fim = time.time() + args.segundos if args.segundos else None
    buf, foto, salvas = b"", None, 0
    try:
        while fim is None or time.time() < fim:
            buf += porta.read(8192)
            while b"\n" in buf:
                bruto, buf = buf.split(b"\n", 1)
                linha = bruto.decode(errors="replace").strip()
                m = INICIO.fullmatch(linha)
                if m:
                    foto = {"tam": int(m[1]), "dim": f"{m[2]}x{m[3]}", "b64": []}
                elif foto is not None and linha == FIM:
                    dados = base64.b64decode("".join(foto["b64"]))
                    if len(dados) != foto["tam"]:
                        print(f"foto incompleta ({len(dados)} de {foto['tam']} bytes) - descartada")
                    else:
                        base = time.strftime("feijao-%Y%m%d-%H%M%S")
                        nome, n = PASTA_SAIDA / f"{base}.jpg", 1
                        while nome.exists():  # duas no mesmo segundo
                            nome, n = PASTA_SAIDA / f"{base}-{n}.jpg", n + 1
                        nome.write_bytes(dados)
                        salvas += 1
                        print(f"{nome.relative_to(RAIZ)}  {foto['dim']}, {len(dados)} bytes")
                    foto = None
                elif foto is not None:
                    foto["b64"].append(linha)
                elif "falhou" in linha.lower():
                    print(f"cérebro: {linha}")
    except KeyboardInterrupt:
        pass
    finally:
        porta.close()
    print(f"{salvas} foto(s) salva(s) em {PASTA_SAIDA.relative_to(RAIZ)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
