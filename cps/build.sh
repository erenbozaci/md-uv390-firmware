#!/bin/sh
# Builds a standalone app for the OS this runs on -> dist/uv390cps (Linux), dist/UV390CPS.app (macOS), dist\uv390cps.exe (Windows)
set -e
cd "$(dirname "$0")"
python3 -m pip install -r requirements.txt -r requirements-build.txt
python3 -m PyInstaller --noconfirm --clean --windowed --name uv390cps --onefile run.py
