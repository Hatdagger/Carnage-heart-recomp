#!/usr/bin/env python3
"""Build the Carnage Heart APK (developer / sandbox entry point).

Runs psxrecomp-android-kit/scripts/build_apk.py with this game's
android_game.json; takes the same arguments (see --help)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KIT = ROOT / "psxrecomp-android-kit" / "scripts" / "build_apk.py"
if not KIT.is_file():
    sys.exit(f"{KIT} is missing (git submodule update --init psxrecomp-android-kit)")
sys.exit(subprocess.call([sys.executable, str(KIT), "--game-root", str(ROOT), *sys.argv[1:]]))
