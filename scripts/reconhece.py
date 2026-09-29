#!/usr/bin/env python3
"""Pede uma foto ao cérebro (serial ou rede) e diz o que a câmera está vendo.

Por que existe: a câmera já sabia tirar foto e despejar em base64 (comando
'p' do console) desde a sessão de montagem, mas ninguém do outro lado sabia
LER a imagem. Este script fecha esse laço: manda 'p', decodifica o base64
que volta entre "---FOTO-INICIO---" e "---FOTO-FIM---", salva o .jpg e
classifica com uma rede treinada em ImageNet — 1000 categorias já rotuladas,
baixadas uma vez e guardadas em cache pelo torch. Não há treino aqui: é
reconhecimento pronto, exatamente o que foi pedido em 29/09/2026.

Cada captura vira uma linha em evidencias/reconhecimento/catalogo.csv, então
o catálogo cresce sozinho a cada objeto mostrado à câmera — é o começo do
banco de imagens rotuladas do robô, mesmo sem treinar nada ainda.

Uso:

    python scripts/reconhece.py --porta COM7
    python scripts/reconhece.py --rede 192.168.1.50          # console por Wi-Fi, porta 23
    python scripts/reconhece.py --porta COM7 --top 3

Precisa de pyserial, torch, torchvision e pillow:

    python -m pip install --user pyserial pillow
    python -m pip install --user --index-url https://download.pytorch.org/whl/cpu torch torchvision

A primeira execução baixa os pesos do MobileNetV2 (≈14 MB) da internet e os
guarda em cache do torch (~/.cache/torch); as próximas rodam offline.
"""
import argparse
import base64
import csv
import datetime
import io
import socket
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
PASTA_SAIDA = RAIZ / "evidencias" / "reconhecimento"
CATALOGO = PASTA_SAIDA / "catalogo.csv"

INICIO = "---FOTO-INICIO"
FIM = "---FOTO-FIM---"


def le_ate_marcador(ler_linha, marcador_prefixo, prazo_s):
    """Lê linhas até achar uma que comece com o marcador, ou estoura o prazo.

    Devolve a própria linha (para o INICIO, que carrega tamanho/largura/altura
    depois do texto). O console também manda log de outras tarefas (sentidos,
    corpo) misturado — por isso não dá para simplesmente ler N linhas fixas.
    """
    ate = time.time() + prazo_s
    while time.time() < ate:
        linha = ler_linha()
        if linha is None:
            continue
        if linha.startswith(marcador_prefixo):
            return linha
    sys.exit(f"não chegou '{marcador_prefixo}' em {prazo_s}s — a câmera respondeu? confira '?' no console")


def captura_serial(porta, baud=115200, prazo_s=20):
    import serial

    s = serial.Serial(porta, baud, timeout=0.5)
    # Abrir a porta reinicia o ESP32 (CP2102/CH340 mexe no DTR). Se houver
    # WIFI_SSID em secrets.h, o setup() tenta a rede por até 10 s ANTES de
    # loop() começar a ler a serial — por isso o prazo aqui é bem mais largo
    # que o da rede (--rede), onde a placa já está de pé havia tempo.
    time.sleep(2)
    s.reset_input_buffer()
    s.write(b"p")

    def ler_linha():
        bruta = s.readline()
        if not bruta:
            return None
        return bruta.decode("ascii", errors="replace").rstrip("\r\n")

    return _consome(ler_linha, prazo_s)


def captura_rede(host, porta=23, prazo_s=8):
    sock = socket.create_connection((host, porta), timeout=prazo_s)
    sock.settimeout(0.5)
    buf = b""

    def ler_linha():
        nonlocal buf
        try:
            buf += sock.recv(4096)
        except socket.timeout:
            pass
        if b"\n" not in buf:
            return None
        linha, _, buf = buf.partition(b"\n")
        return linha.decode("ascii", errors="replace").rstrip("\r")

    sock.sendall(b"p")
    try:
        return _consome(ler_linha, prazo_s)
    finally:
        sock.close()


def _consome(ler_linha, prazo_s):
    linha_inicio = le_ate_marcador(ler_linha, INICIO, prazo_s)
    partes = linha_inicio.split()
    # "---FOTO-INICIO <len> <w> <h>---"
    tamanho_esperado, largura, altura = int(partes[1]), int(partes[2]), int(partes[3])

    pedacos = []
    ate = time.time() + prazo_s
    while time.time() < ate:
        linha = ler_linha()
        if linha is None:
            continue
        if linha.startswith(FIM):
            break
        pedacos.append(linha)
    else:
        sys.exit(f"a foto não terminou em {prazo_s}s — chegaram {sum(len(p) for p in pedacos)} de {tamanho_esperado} bytes de base64")

    dados = base64.b64decode("".join(pedacos))
    if abs(len(dados) - tamanho_esperado) > 2:  # até 2 bytes de folga do padding
        print(f"aviso: {len(dados)} bytes decodificados, câmera avisou {tamanho_esperado}", file=sys.stderr)
    return dados, largura, altura


def classifica(jpeg_bytes, top_k):
    # Import tardio: carregar torch custa ~1-2 s, e não faz sentido pagar
    # esse preço antes de a captura ter sucesso.
    import torch
    from torchvision.io import decode_jpeg
    from torchvision.models import MobileNet_V2_Weights, mobilenet_v2

    pesos = MobileNet_V2_Weights.DEFAULT
    modelo = mobilenet_v2(weights=pesos)
    modelo.eval()

    img = decode_jpeg(torch.frombuffer(bytearray(jpeg_bytes), dtype=torch.uint8))
    if img.shape[0] == 1:  # mono vira RGB triplicando o canal
        img = img.expand(3, -1, -1)
    entrada = pesos.transforms()(img).unsqueeze(0)

    with torch.no_grad():
        saida = modelo(entrada)
    probs = torch.nn.functional.softmax(saida[0], dim=0)
    valores, indices = probs.topk(top_k)
    categorias = pesos.meta["categories"]
    return [(categorias[i], float(v)) for v, i in zip(valores, indices)]


def salva_catalogo(caminho_jpg, largura, altura, previsoes):
    PASTA_SAIDA.mkdir(parents=True, exist_ok=True)
    novo = not CATALOGO.exists()
    with open(CATALOGO, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if novo:
            w.writerow(["quando", "arquivo", "largura", "altura", "rotulo_1", "confianca_1", "rotulo_2", "confianca_2", "rotulo_3", "confianca_3"])
        linha = [datetime.datetime.now().isoformat(timespec="seconds"), caminho_jpg.name, largura, altura]
        for rotulo, conf in previsoes[:3]:
            linha += [rotulo, f"{conf:.4f}"]
        while len(linha) < 10:
            linha += ["", ""]
        w.writerow(linha)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    grupo = ap.add_mutually_exclusive_group(required=True)
    grupo.add_argument("--porta", help="porta serial do cérebro, ex. COM7")
    grupo.add_argument("--rede", help="IP do cérebro na rede (console telnet, porta 23)")
    ap.add_argument("--top", type=int, default=5, help="quantas categorias mostrar (padrão 5)")
    args = ap.parse_args()

    print("[reconhece] pedindo foto...")
    if args.porta:
        jpeg_bytes, largura, altura = captura_serial(args.porta)
    else:
        jpeg_bytes, largura, altura = captura_rede(args.rede)
    print(f"[reconhece] recebida: {len(jpeg_bytes)} bytes, {largura}x{altura}")

    PASTA_SAIDA.mkdir(parents=True, exist_ok=True)
    nome = f"{datetime.datetime.now().strftime('%Y%m%d-%H%M%S')}.jpg"
    caminho = PASTA_SAIDA / nome
    caminho.write_bytes(jpeg_bytes)
    print(f"[reconhece] salva em {caminho}")

    print("[reconhece] classificando (primeira vez baixa os pesos, ~14 MB)...")
    previsoes = classifica(jpeg_bytes, args.top)
    for rotulo, conf in previsoes:
        print(f"    {conf * 100:5.1f}%  {rotulo}")

    salva_catalogo(caminho, largura, altura, previsoes)
    print(f"[reconhece] registrado em {CATALOGO}")


if __name__ == "__main__":
    main()
