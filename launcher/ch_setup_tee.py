"""Carnage Heart: run a setup command, show its output and save it to the log.

On Windows the setup wizard exits and a console (recomp_deferred_rebuild.cmd)
finishes the build. cmake/ch_setup_log.cmake makes that console run each step
through this script so the output also lands in <game folder>/setup-log.txt,
next to what the wizard itself logged.

Usage: python ch_setup_tee.py <log file> <command> [args...]
Exit code: the command's own exit code (the console checks it).
"""
import datetime
import os
import subprocess
import sys

MAX_BYTES = 4 * 1024 * 1024


def open_log(path):
    try:
        if os.path.getsize(path) > MAX_BYTES:
            old = os.path.join(os.path.dirname(path), "setup-log.old.txt")
            if os.path.exists(old):
                os.remove(old)
            os.replace(path, old)
    except OSError:
        pass
    try:
        return open(path, "a", encoding="utf-8", errors="replace")
    except OSError:
        return None


def step_name(cmd):
    # "python psxrecomp_cli.py rebuild ..." -> "rebuild"
    for arg in cmd[1:]:
        if not arg.startswith("-") and not arg.lower().endswith(".py"):
            return arg
    return os.path.basename(cmd[0])


def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: ch_setup_tee.py <log> <command> [args...]\n")
        return 2
    log = open_log(argv[1])
    cmd = argv[2:]
    if log:
        now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log.write("\n===== %s  Console %s (after the wizard closed) =====\n"
                  % (now, step_name(cmd)))
        log.write("Command: %s\n" % subprocess.list2cmdline(cmd))
        log.flush()
    env = dict(os.environ, PYTHONUNBUFFERED="1")
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=env)
    except OSError as exc:
        msg = "Could not start %s: %s\n" % (cmd[0], exc)
        sys.stdout.write(msg)
        if log:
            log.write(msg)
            log.close()
        return 1
    out = getattr(sys.stdout, "buffer", None)
    for raw in iter(proc.stdout.readline, b""):
        if out is not None:
            out.write(raw)
            out.flush()
        else:
            sys.stdout.write(raw.decode("utf-8", "replace"))
            sys.stdout.flush()
        if log:
            log.write(raw.decode("utf-8", "replace").rstrip("\r\n") + "\n")
            log.flush()
    code = proc.wait()
    if log:
        log.write("----- %s -----\n" % ("step finished OK" if code == 0
                                         else "STEP FAILED (exit %d)" % code))
        log.close()
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
