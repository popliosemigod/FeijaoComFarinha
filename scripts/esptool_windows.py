"""PlatformIO, antes de compilar: no Windows, chama o esptool pelo Python.

A plataforma (pioarduino) roda o esptool pelo `esptool.exe` que o pip cria em
`penv/Scripts/`. Em 05/10/2026 o Controle Inteligente de Aplicativos do Windows
(Device Guard) passou a bloquear esse .exe - ele nao e assinado - e toda
compilacao parou em `bootloader.bin` com `Error 4551`, sem dizer o porque.

O mesmo esptool roda pelo Python do penv, que o Windows nao bloqueia. Este
script troca o .exe por um `esptool.cmd` de uma linha (`python -m esptool`),
para a compilacao e para a gravacao. Fora do Windows nao faz nada: no Linux e
no CI o .exe nem existe.
"""
import os
import sys

Import("env")  # noqa: F821 - injetado pelo PlatformIO

if sys.platform == "win32":
    plataforma = type(env.PioPlatform())  # noqa: F821
    original = plataforma.setup_python_env

    def pelo_python(self, *args, **kwargs):
        python, esptool = original(self, *args, **kwargs)
        if not esptool.lower().endswith(".exe"):
            return python, esptool
        cmd = esptool[:-4] + ".cmd"
        if not os.path.isfile(cmd):
            with open(cmd, "w", encoding="ascii") as f:
                f.write('@"%~dp0python.exe" -m esptool %*\n')
        return python, cmd

    if not getattr(original, "_feijao", False):
        pelo_python._feijao = True
        plataforma.setup_python_env = pelo_python
