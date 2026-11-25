"""Reads a game's android_game.json (see docs/SETTINGS.md) and fills defaults.

Used by build_apk.py and build_android_app.py. The game root is the folder
holding android_game.json; by default the parent of the kit folder (the kit
is a submodule at <game>/psxrecomp-android-kit)."""
import json
import re
from pathlib import Path

KIT = Path(__file__).resolve().parents[1]

DEFAULTS = {
    "app_name": "PSXRecomp game",
    "log_tag": "PSXRecomp",
    "log_name": "game",
    "exe_name": "psx_game",
    "default_scale": "3x",
    "accent": "#7CFF9A",
    "background": "#06140A",
    "res_dir": "android/res",
    "java_dir": "",
    "java_hooks": "",
    "extra_buttons": [],
    "permissions": [],
    "manifest_application_xml": "",
    "game_files": ["game.toml", "game_options.toml", "bios", "mods", "assets"],
    "generated_check": "",
    "data_dir_name": "",
    "launcher_alias": "",
}

REQUIRED = ("package", "app_name")


def default_root() -> Path:
    return KIT.parent


def load(root: Path) -> dict:
    path = root / "android_game.json"
    if not path.is_file():
        raise SystemExit(f"{path} is missing. Copy android_game.example.json from "
                         f"{KIT.name} to your game's folder and fill it in.")
    raw = json.loads(path.read_text(encoding="utf-8"))
    s = dict(DEFAULTS)
    s.update({k: v for k, v in raw.items() if not k.startswith("_")})
    for k in REQUIRED:
        if not raw.get(k):
            raise SystemExit(f"{path.name}: \"{k}\" is required")
    if not re.fullmatch(r"[a-zA-Z][\w]*(\.[a-zA-Z][\w]*)+", s["package"]):
        raise SystemExit(f"{path.name}: package \"{s['package']}\" is not a valid "
                         "Android package name (e.g. com.yourname.gamename)")
    if s["default_scale"] not in ("2x", "3x", "4x"):
        raise SystemExit(f"{path.name}: default_scale must be 2x, 3x or 4x")
    short = re.sub(r"[^A-Za-z0-9]", "", s["app_name"]) or "Game"
    s.setdefault("title", s["app_name"].upper())
    s.setdefault("apk_name", short + ".apk")
    s.setdefault("keystore_name", short.lower() + ".keystore")
    s.setdefault("key_alias", short.lower())
    s.setdefault("key_pass", short.lower())
    s.setdefault("cert_name", f"{s['app_name']} Recompiled (personal build)")
    s.setdefault("disc_prompt",
                 f"Choose your {s['app_name']} disc.\n\n"
                 "Select the .cue file and all of its .bin files together\n"
                 "(tap and hold the first one, then tap the others),\n"
                 "or a single .chd file.\n\n"
                 "They are copied into the app once.")
    if not s["data_dir_name"]:
        s["data_dir_name"] = short
    s["java_hooks"] = full_class(s["package"], s["java_hooks"])
    s["launcher_alias"] = full_class(s["package"], s["launcher_alias"])
    s["extra_buttons"] = [
        {"label": b["label"], "activity": full_class(s["package"], b["activity"])}
        for b in s["extra_buttons"]]
    return s


def full_class(package: str, name: str) -> str:
    """'.Foo' -> 'com.x.y.Foo'; full names and '' unchanged."""
    if name.startswith("."):
        return package + name
    return name
