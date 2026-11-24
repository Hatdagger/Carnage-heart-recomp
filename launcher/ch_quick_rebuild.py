"""Carnage Heart: rebuild the game after an update, without regenerating it.

An update that changes only the launcher, the hooks in src/, the build files
or other things that are compiled (not the recompiler, the symbols or the
seeds) does not need new game C: the C already in generated/ came from the
player's disc and is still right. The updater (launcher/ch_update.cpp) keeps
generated/ in that case and starts this script in its own console window
instead of the setup wizard. It:

  1. waits for the launcher that started it to close (so its exe can be
     replaced on Windows);
  2. runs the same two psxrecomp steps the wizard's rebuild console runs
     (ensure-toolchain, then rebuild without PGO), with the output shown and
     saved to setup-log.txt;
  3. starts the rebuilt game's launcher.

If the build fails, it offers the full rebuild instead: it removes generated/
and starts the setup exe, which regenerates the game from the disc.

Usage: python ch_quick_rebuild.py <kit root> <launcher pid> [toolchain bin dir]
Standard library only (it runs on the compiler pack's Python).
"""
import datetime
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ch_setup_tee  # noqa: E402  (same folder)

TARGET = "psx-runtime"                      # codegen_setup.c .cmake_target
EXE_BASE = "CarnageHeart_Recompiled"        # codegen_setup.c .exe_basename
GEN_MARKER = os.path.join("generated", "SCUS_946.04_dispatch.c")
WINDOWS = os.name == "nt"


def say(msg=""):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


def pid_alive(pid):
    if pid <= 0:
        return False
    if WINDOWS:
        import ctypes
        SYNCHRONIZE = 0x00100000
        k32 = ctypes.windll.kernel32
        h = k32.OpenProcess(SYNCHRONIZE, False, pid)
        if not h:
            return False
        try:
            return k32.WaitForSingleObject(h, 0) == 0x102  # WAIT_TIMEOUT
        finally:
            k32.CloseHandle(h)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def wait_for(pid, limit=120.0):
    start = time.time()
    while pid_alive(pid) and time.time() - start < limit:
        time.sleep(0.5)


def log_note(log_path, text):
    try:
        with open(log_path, "a", encoding="utf-8", errors="replace") as f:
            now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            f.write("\n===== %s  %s =====\n" % (now, text))
    except OSError:
        pass


def game_exe(root):
    build = os.path.join(root, "build-release")
    name = EXE_BASE
    try:
        with open(os.path.join(build, "psxrecomp_exe_name-%s.txt" % TARGET),
                  encoding="utf-8") as f:
            published = f.readline().strip()
            if published:
                name = published
    except OSError:
        pass
    exe = os.path.join(build, name + (".exe" if WINDOWS else ""))
    return exe if os.path.isfile(exe) else None


def start_detached(exe, args, cwd):
    if WINDOWS:
        DETACHED_PROCESS = 0x00000008
        subprocess.Popen([exe] + args, cwd=cwd, creationflags=DETACHED_PROCESS,
                         close_fds=True)
    else:
        subprocess.Popen([exe] + args, cwd=cwd, start_new_session=True,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)


def pause(prompt):
    try:
        return input(prompt)
    except (EOFError, KeyboardInterrupt):
        return ""


def full_rebuild(root):
    """Fall back to what updates did before: regenerate from the disc."""
    setup = os.path.join(root, EXE_BASE + (".exe" if WINDOWS else ""))
    shutil.rmtree(os.path.join(root, "generated"), ignore_errors=True)
    if os.path.isfile(setup):
        say("Starting the full rebuild (it makes the game again from your disc)...")
        start_detached(setup, [], root)
        return 0
    say("Could not find %s. Start it yourself to finish the update." % setup)
    pause("Press Enter to close. ")
    return 1


def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: ch_quick_rebuild.py <kit root> <pid> [toolchain bin]\n")
        return 2
    root = os.path.abspath(argv[1])
    pid = int(argv[2]) if argv[2].isdigit() else 0
    if len(argv) > 3 and argv[3]:
        os.environ["PATH"] = argv[3] + os.pathsep + os.environ.get("PATH", "")
    if WINDOWS:
        os.system("title Carnage Heart - updating")

    say("Carnage Heart update: rebuilding the game.")
    say("Your game code from the disc is kept, so this is quicker than the first install.")
    say()
    say("Waiting for the launcher to close...")
    wait_for(pid)

    log = os.path.join(root, "setup-log.txt")
    log_note(log, "Update: quick rebuild (game code kept)")
    cli = os.path.join(root, "psxrecomp", "psxrecomp_cli.py")
    py = sys.executable or "python"
    steps = [
        [py, cli, "ensure-toolchain", "--project-root", root],
        [py, cli, "rebuild", "--project-root", root,
         "--config", os.path.join(root, "game.toml"),
         "--build-dir", os.path.join(root, "build-release"),
         "--target", TARGET, "--exe-basename", EXE_BASE,
         "--no-pgo", "--prune-after", "build-intermediates"],
    ]
    os.chdir(root)
    ok = os.path.isfile(cli) and os.path.isfile(os.path.join(root, GEN_MARKER))
    for i, cmd in enumerate(steps):
        if not ok:
            break
        say()
        say("Step %d of %d: %s..." % (i + 1, len(steps),
                                      "checking the compiler pack" if i == 0 else "building"))
        ok = ch_setup_tee.main(["ch_setup_tee.py", log] + cmd) == 0

    exe = game_exe(root) if ok else None
    if exe:
        say()
        say("Finished. Starting Carnage Heart...")
        start_detached(exe, ["--launcher"], root)
        time.sleep(1.0)
        return 0

    say()
    say("The quick rebuild did not work. The details are in:")
    say("  " + log)
    say()
    answer = pause("Press Enter to do the full rebuild instead (a few minutes longer), "
                   "or type N and Enter to stop: ")
    if answer.strip().lower().startswith("n"):
        return 1
    return full_rebuild(root)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
