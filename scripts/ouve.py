#!/usr/bin/env python3
"""Ouve o robô pelo USB e escreve o que foi dito (fala para texto).

A placa recorta as frases (`src/voz_serial.h`): decide onde cada uma começa e
acaba e despeja o áudio em base64 no console, entre "---AUDIO-INICIO---" e
"---AUDIO-FIM---". Este script é a outra metade: junta o áudio, transcreve com
o Whisper **nesta máquina** e mostra o texto.

É o caminho 1 de docs/05-a-voz.md — o mesmo motor (faster-whisper) que a ponte
do Jaspy usa —, só que pelo cabo, porque o Wi-Fi do robô ainda não tem
credencial. Nada sai do computador: não há serviço de nuvem nem chave de API.

Uso:

    python scripts/ouve.py                       # COM13, modelo small
    python scripts/ouve.py --porta COM13 --modelo base
    python scripts/ouve.py --segundos 60         # para sozinho depois de 60 s

Cada frase vira um .wav em evidencias/voz/ e uma linha em
evidencias/voz/transcricoes.csv (os dois fora do git).

Precisa de pyserial, numpy e faster-whisper:

    python -m pip install --user pyserial numpy faster-whisper

A primeira execução baixa o modelo (≈460 MB no `small`) e guarda em cache;
as próximas rodam offline.
"""
import argparse
import base64
import csv
import datetime
import re
import sys
import time
import wave
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
PASTA_SAIDA = RAIZ / "evidencias" / "voz"
CATALOGO = PASTA_SAIDA / "transcricoes.csv"

INICIO = re.compile(r"---AUDIO-INICIO (\d+) (\d+)---")
FIM = "---AUDIO-FIM---"
BASE64 = re.compile(r"[A-Za-z0-9+/]+={0,2}")


def carrega_modelo(tamanho):
    from faster_whisper import WhisperModel

    print(f"carregando o Whisper ({tamanho})...", flush=True)
    t0 = time.time()
    modelo = WhisperModel(tamanho, device="cpu", compute_type="int8")
    print(f"pronto em {time.time() - t0:.1f} s", flush=True)
    return modelo


def transcreve(modelo, pcm, taxa, idioma):
    """Devolve (texto, segundos gastos). `pcm` é int16 mono."""
    import numpy as np

    audio = pcm.astype(np.float32) / 32768.0
    audio -= audio.mean()  # o microfone tem offset DC
    pico = float(np.abs(audio).max())
    if pico > 0:
        audio *= min(0.9 / pico, 20.0)  # o PDM chega baixo; o Whisper prefere volume cheio
    if taxa != 16000:
        raise ValueError(f"o Whisper quer 16 kHz, veio {taxa}")

    t0 = time.time()
    segmentos, _ = modelo.transcribe(audio, language=idioma, beam_size=5,
                                     condition_on_previous_text=False)
    texto = " ".join(s.text.strip() for s in segmentos).strip()
    return texto, time.time() - t0


def salva(pcm, taxa, texto, gasto):
    PASTA_SAIDA.mkdir(parents=True, exist_ok=True)
    agora = datetime.datetime.now()
    nome = PASTA_SAIDA / f"frase-{agora:%Y%m%d-%H%M%S}.wav"
    with wave.open(str(nome), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(taxa)
        w.writeframes(pcm.tobytes())
    novo = not CATALOGO.exists()
    with CATALOGO.open("a", newline="", encoding="utf-8") as f:
        c = csv.writer(f)
        if novo:
            c.writerow(["quando", "arquivo", "duracao_s", "transcricao_s", "texto"])
        c.writerow([agora.isoformat(timespec="seconds"), nome.name,
                    f"{len(pcm) / taxa:.2f}", f"{gasto:.2f}", texto])
    return nome


def main():
    import numpy as np
    import serial

    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--porta", default="COM13", help="porta USB do cérebro")
    ap.add_argument("--modelo", default="small", help="tiny, base, small, medium...")
    ap.add_argument("--idioma", default="pt")
    ap.add_argument("--segundos", type=float, default=0, help="para sozinho depois disso (0 = nunca)")
    ap.add_argument("--log", action="store_true", help="mostra também o log da placa")
    args = ap.parse_args()

    modelo = carrega_modelo(args.modelo)

    porta = serial.Serial()
    porta.port, porta.baudrate, porta.timeout = args.porta, 115200, 0.1
    porta.dtr = porta.rts = False  # nao reiniciar a placa ao abrir
    porta.open()
    time.sleep(0.2)
    porta.reset_input_buffer()
    porta.write(b"o")
    print("ouvindo - fale perto do robô (Ctrl+C para sair)\n", flush=True)

    fim = time.time() + args.segundos if args.segundos else None
    pendente = b""
    amostras = taxa = None
    linhas_b64 = []
    frases = 0
    try:
        while fim is None or time.time() < fim:
            pendente += porta.read(65536)
            while b"\n" in pendente:
                bruta, pendente = pendente.split(b"\n", 1)
                linha = bruta.decode(errors="replace").strip()

                m = INICIO.fullmatch(linha)
                if m:
                    amostras, taxa = int(m.group(1)), int(m.group(2))
                    linhas_b64 = []
                    continue
                if amostras is None:
                    if args.log and linha:
                        print(f"    . {linha}")
                    continue
                if linha != FIM:
                    # Log de outra task pode cair entre duas linhas de audio.
                    if BASE64.fullmatch(linha):
                        linhas_b64.append(linha)
                    elif args.log and linha:
                        print(f"    . {linha}")
                    continue

                pcm = np.frombuffer(base64.b64decode("".join(linhas_b64)), dtype="<i2")
                esperado, amostras = amostras, None
                if len(pcm) != esperado:
                    print(f"  (frase incompleta: {len(pcm)} de {esperado} amostras - descartada)")
                    continue
                texto, gasto = transcreve(modelo, pcm, taxa, args.idioma)
                salva(pcm, taxa, texto, gasto)
                frases += 1
                print(f"[{datetime.datetime.now():%H:%M:%S}] {len(pcm) / taxa:4.1f} s de fala, "
                      f"{gasto:.1f} s para transcrever:  {texto or '(nada reconhecido)'}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            porta.write(b"q")
            porta.flush()
        finally:
            porta.close()
    print(f"\n{frases} frase(s). Áudio e transcrições em {PASTA_SAIDA}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
