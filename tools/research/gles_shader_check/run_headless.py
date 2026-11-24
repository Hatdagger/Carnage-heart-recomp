#!/usr/bin/env python3
"""Drive a CH_GLES_DESKTOP_TEST Linux build headless on Mesa (OpenGL ES 3.2
through the Android bridge) and take screenshots via the debug server.

  python3 run_headless.py BUILD_DIR DISC.cue OUT_DIR [--scale native|4x ...]

Steps: boot, wait, press Start/Cross a few times to reach the menus, and
save present_shot PNGs + the log in OUT_DIR. The PS1 pad bits used by the
debug server's "press": START 0x0008, CROSS 0x4000, CIRCLE 0x2000, UP 0x0010,
DOWN 0x0040.
"""
import json
import os
import socket
import subprocess
import sys
import time

build, cue, out = sys.argv[1], sys.argv[2], sys.argv[3]
res = sys.argv[4] if len(sys.argv) > 4 else "4x"
os.makedirs(out, exist_ok=True)
env = dict(os.environ)
env.update({
    "SDL_VIDEO_DRIVER": "offscreen",
    "SDL_AUDIO_DRIVER": "dummy",
    "EGL_PLATFORM": "surfaceless",
    "PSX_FRAME_REPORT_MS": "2000",
    "PSX_DEBUG_SERVER": "1",
    "PSX_INTERNAL_RESOLUTION": res,
})
exe = os.path.join(build, "CarnageHeart_Recompiled")
log = open(os.path.join(out, "run.log"), "w")
p = subprocess.Popen([exe, "--no-launcher", "--renderer", "opengl", "--disc", cue],
                     cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT)
PORT = 4370
seq = [0]


def cmd(obj, timeout=10):
    seq[0] += 1
    obj["id"] = seq[0]
    with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as s:
        s.sendall((json.dumps(obj) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    try:
        return json.loads(buf.decode(errors="replace").strip().splitlines()[-1])
    except Exception:
        return {"raw": buf[:300]}


def wait_server(t=60):
    end = time.time() + t
    while time.time() < end:
        if p.poll() is not None:
            return False
        try:
            cmd({"cmd": "ping"}, timeout=2)
            return True
        except OSError:
            time.sleep(1)
    return False


def shot(name):
    r = cmd({"cmd": "present_shot", "path": os.path.abspath(os.path.join(out, name + ".png"))})
    time.sleep(1.5)
    print(name, r)


def press(buttons, frames=6, wait=1.5):
    print("press", hex(buttons), cmd({"cmd": "press", "buttons": buttons, "frames": frames}))
    time.sleep(wait)


try:
    if not wait_server():
        print("debug server did not come up; exit code", p.poll())
        sys.exit(1)
    plan = os.environ.get("PLAN", "s20 shot:boot s10 shot:title p8 s4 shot:after_start p4000 s4 shot:menu1 p4000 s5 shot:menu2 p4000 s6 shot:menu3")
    for step in plan.split():
        if p.poll() is not None:
            print("game exited", p.returncode)
            break
        if step.startswith("shot:"):
            shot(step[5:])
        elif step.startswith("s"):
            time.sleep(float(step[1:]))
        elif step.startswith("p"):
            press(int(step[1:], 16))
finally:
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
    log.close()
