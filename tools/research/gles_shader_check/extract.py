#!/usr/bin/env python3
"""Pull every GLSL shader string literal (starting with '#version 330') out of a
preprocessed gpu_gl_renderer.c and write check.c's input file.

  clang -E -P -DPSX_SDL3 -I... psxrecomp/runtime/src/gpu_gl_renderer.c -o /tmp/gl_pp.c
  python3 extract.py /tmp/gl_pp.c shaders.bin

Stage: a source that writes gl_Position is a vertex shader."""
import re
import sys

src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
lit = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
toks = [(m.start(), m.end(), m.group(1)) for m in lit.finditer(src)]


def unescape(s):
    return bytes(s, "utf-8").decode("unicode_escape")


out = []
i = 0
while i < len(toks):
    s, e, body = toks[i]
    if body.startswith("#version 330"):
        parts, j, end = [body], i + 1, e
        while j < len(toks) and src[end:toks[j][0]].strip() == "":
            parts.append(toks[j][2])
            end = toks[j][1]
            j += 1
        m = re.search(r'(\w+)\s*(?:\[\s*\])?\s*=\s*$', src[max(0, s - 200):s])
        name = m.group(1) if m else f"anon_{len(out)}"
        text = unescape("".join(parts))
        if "%s" not in text:  # snprintf templates are filled at run time
            out.append((name, "v" if "gl_Position" in text else "f", text))
        i = j
    else:
        i += 1

with open(sys.argv[2], "wb") as f:
    for name, stage, text in out:
        f.write(name.encode() + b"\0" + stage.encode() + b"\0" + text.encode() + b"\0")
print(len(out), "shaders")
