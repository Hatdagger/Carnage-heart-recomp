#!/usr/bin/env python3
"""Build CarnageHeart.apk on the player's PC (run by "Build Android App.bat" /
build-android-app.sh in the setup kit).

The work is done by the PSXRecomp Android kit
(psxrecomp-android-kit/scripts/build_android_app.py) with this game's
settings in android_game.json. This file stays here because the launcher's
BUILD ANDROID APP row and the kit launchers look for it."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KIT = ROOT / "psxrecomp-android-kit" / "scripts" / "build_android_app.py"
if not KIT.is_file():
    sys.exit(f"{KIT} is missing (git submodule update --init psxrecomp-android-kit)")
# Older Carnage Heart builds used CH_ANDROID_HOME for the tools folder.
if os.environ.get("CH_ANDROID_HOME") and not os.environ.get("PSXAK_ANDROID_HOME"):
    os.environ["PSXAK_ANDROID_HOME"] = os.environ["CH_ANDROID_HOME"]
sys.exit(subprocess.call([sys.executable, str(KIT), "--game-root", str(ROOT), *sys.argv[1:]]))
