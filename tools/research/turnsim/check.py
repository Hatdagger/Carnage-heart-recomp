"""Checks two simulator logs (A.log, B.log): every turn file loaded shows the
sender's side exactly as the sender left it, and the turn numbers line up."""
import sys, re
def parse(path):
    pre, post = [], []
    for line in open(path):
        m = re.match(r'(PRE|POST) [AB] (mine|theirs) +turn=(\d+) (.*)', line.strip())
        if not m: continue
        tag, who, turn, rest = m.groups()
        if tag == 'PRE' and who == 'mine': pre.append((int(turn), rest))
        if tag == 'POST' and who == 'mine': post.append({'turn': int(turn), 'mine': rest})
        if tag == 'POST' and who == 'theirs': post[-1]['theirs'] = rest
    return pre, post
a_pre, a_post = parse(sys.argv[1]); b_pre, b_post = parse(sys.argv[2])
off = int(sys.argv[3]) if len(sys.argv) > 3 else 0   # B's log starts after a restart: A had sent this many files
bad = 0
def same(what, x, y):
    global bad
    ok = x == y
    if not ok: bad += 1
    print(('ok  ' if ok else 'FAIL'), what, '' if ok else '\n   sent: %s\n   seen: %s' % (x, y))
# A's End Turn i sends file 2i-1; B's End Turn j (j>=1) loads file 2j-1.
# After a restart B's first End Turn only receives, and loads A's next file.
for j, bp in enumerate(b_post, 1):
    J = j + off
    if J - 1 < len(a_pre):
        same('file %d: B sees A as A left it' % (2*J-1), a_pre[J-1][1], bp['theirs'])
        same('file %d: B plays turn %d' % (2*J-1, J), J, bp['turn'])
# B's End Turn j>=2 sends file 2(j-1); A's End Turn i loads file 2i.
for i, ap in enumerate(a_post, 1):
    j = i - off + 1          # B's End Turn (in this log) that sent file 2i
    if 2 <= j <= len(b_pre):
        same('file %d: A sees B as B left it' % (2*i), b_pre[j-1][1], ap['theirs'])
    if j >= 2 or off == 0:
        same('file %d: A plays turn %d' % (2*i, i+1), i+1, ap['turn'])
print('FAILED: %d' % bad if bad else 'all checks passed')
sys.exit(1 if bad else 0)
