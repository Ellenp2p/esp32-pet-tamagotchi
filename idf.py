#!/usr/bin/env python
"""Wrapper to run ESP-IDF idf.py from git-bash on this machine.

The ESP-IDF install here is non-standard (tools under C:/Espressif/tools,
venv under the tools dir), so a plain `. export.sh` fails, and idf_tools.py
refuses to run under MSYS. This wrapper strips the MSYS markers, sets the
IDF env vars, and prepends the known tool bin dirs to PATH.

Usage: python idf.py <build|flash|monitor|-p COM5 flash|...>
"""
import glob
import os
import subprocess
import sys

VENV_PY = r"C:/Espressif/tools/python/v6.0.2/venv/Scripts/python.exe"
IDF_PATH = r"C:/esp/v6.0.2/esp-idf"
IDF_TOOLS_PATH = r"C:/Espressif/tools"
IDF_PY = IDF_PATH + r"/tools/idf.py"
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))

env = os.environ.copy()
env.pop("MSYSTEM", None)
env.pop("MSYS2_PATH_TYPE", None)
env["IDF_PYTHON_ENV_PATH"] = r"C:/Espressif/tools/python/v6.0.2/venv"
env["IDF_TOOLS_PATH"] = IDF_TOOLS_PATH
env["IDF_PATH"] = IDF_PATH
env["ESP_IDF_VERSION"] = "6.0"

# Prepend tool bin dirs (newest match of each) to PATH.
tool_globs = [
    "ninja/*",
    "cmake/*/bin",
    "xtensa-esp-elf/*/xtensa-esp-elf/bin",
    "riscv32-esp-elf/*/riscv32-esp-elf/bin",
    "esp-rom-elfs/*",
    "openocd-esp32/*/openocd-esp32/bin",
]
extra = []
for g in tool_globs:
    matches = sorted(glob.glob(os.path.join(IDF_TOOLS_PATH, g)))
    if matches:
        extra.append(os.path.normpath(matches[-1]))
extra.append(os.path.join(IDF_PATH, "tools"))
env["PATH"] = os.pathsep.join(extra) + os.pathsep + env.get("PATH", "")

args = [VENV_PY, IDF_PY] + sys.argv[1:]
result = subprocess.run(args, env=env, cwd=PROJECT_DIR)
print("returncode", result.returncode)
sys.exit(result.returncode)
