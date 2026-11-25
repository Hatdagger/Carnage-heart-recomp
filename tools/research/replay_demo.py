"""Replay test (multiplayer plan step 7): watch what the opponent did.

Two real copies through a local relay (as online_demo.py; needs a build
with -DCH_REMOTE_BATTLE=ON and Node). A forms a unit on Defend, B forms one
and sends it to capture A's start base; on A's turn 4 A's defender meets B's
attacker and they fight on A's copy. Each copy that receives a turn file
replays it: the opponent's units walk on the map and their battle is fought
again from the same seed. Screenshots are taken while a replay runs
(build/research/replay/<copy>-file<N>-<i>.png, and a contact sheet per
replay).

    python3 tools/research/replay_demo.py            # B watches the battle when A's file arrives
    REPLAY_RESTART=1 python3 tools/research/replay_demo.py
        # B closes after sending turn file 6 and starts again once A has
        # played turn 4: the battle is replayed when the map opens

Checks: every replay ends with the units where the turn file says, the
replayed battle gives the recorded result, and no desyncs. (A wins the
battle and only defends, so the game runs until the rounds run out.)
"""
import os, re, sys, time, shutil, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ch, hotseat, online_demo as od

OUT = os.path.join(od.BUILD, 'research', 'replay')
od.OUT = OUT
RESTART = os.environ.get('REPLAY_RESTART') == '1'
SHOTS_EVERY = int(os.environ.get('REPLAY_SHOT_FRAMES', '45'))


def watch_replay(p, seq, timeout=900):
    """Screenshots of copy p while it replays turn file seq. Returns the
    'replay over' line, or None if there was no replay."""
    start_line = '[turns] replay of turn file %d:' % seq
    t = time.time()
    while start_line not in od.log(p):
        if 'loaded turn file %d:' % seq in od.log(p) or time.time() - t > timeout:
            return None
        time.sleep(0.2)
    od.use(p)
    shots = []
    n_over = od.log(p).count('[turns] replay over')
    before = od.log(p)[:od.log(p).index(start_line)].count('[turns] replay over')
    i = 0
    while od.log(p).count('[turns] replay over') <= before:
        if time.time() - t > timeout:
            raise SystemExit('replay of turn file %d on %s did not finish' % (seq, p))
        path = os.path.join(OUT, '%s-file%d-%02d.png' % (p, seq, i))
        try:
            ch.shot(path)
            shots.append(path)
        except Exception:
            pass
        i += 1
        ch.wait(SHOTS_EVERY)
    contact(shots, os.path.join(OUT, '%s-file%d-sheet.png' % (p, seq)))
    lines = [l for l in od.log(p).splitlines() if l.startswith('[turns] replay')]
    over = [l for l in lines if l.startswith('[turns] replay over')][before]
    print('%s replayed turn file %d (%d screenshots): %s' % (p, seq, len(shots), over[len('[turns] '):]))
    return over


def contact(shots, path):
    if not shots:
        return
    from PIL import Image
    ims = [Image.open(s).convert('RGB') for s in shots[:48]]
    w, h = 320, int(320 * ims[0].height / ims[0].width)
    cols = 6
    sheet = Image.new('RGB', (cols * w, ((len(ims) + cols - 1) // cols) * h))
    for k, im in enumerate(ims):
        sheet.paste(im.resize((w, h)), ((k % cols) * w, (k // cols) * h))
    sheet.save(path)


def boot_into_replay(p):
    """boot_europa.py's presses up to choosing Europa (Practice); the map
    start then replays the opponent's turn, so wait for it before the
    presses that get to the Command menu."""
    od.use(p)
    ch.press('start', hold=8, after=20); ch.wait(150)
    for _ in range(3):
        ch.press('x', hold=8, after=120)
    ch.press('x', hold=8, after=20)


def finish_boot(p):
    od.use(p)
    ch.wait(120)
    ch.press('circle', hold=8, after=60); ch.press('start', hold=8, after=60)
    ch.press('circle', hold=8, after=40)


def check(what, ok):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        raise SystemExit(1)


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    relay = None
    if 'CH_RELAY_URL' not in os.environ:
        relay = subprocess.Popen(['node', os.path.join(od.REPO, 'relay', 'dev_server.mjs'), '8787'],
                                 stdout=open(os.path.join(OUT, 'relay.log'), 'wb'), stderr=subprocess.STDOUT)
        time.sleep(1)
    overs = []
    try:
        g = od.post(od.RELAY + '/v1/games')
        game, keyA = g['game'], g['key']
        keyB = od.post(od.RELAY + '/v1/games/%s/join' % game)['key']
        fa, fb = os.path.join(OUT, 'turnsA'), os.path.join(OUT, 'turnsB')
        print('game', game, '(restart before the battle)' if RESTART else '')
        od.start('A', game, keyA, fa, '1')
        od.start('B', game, keyB, fb, '1')
        od.boot('A'); od.boot('B')
        od.wait_log('A', 'map start: no turn files yet'); od.wait_log('B', 'map start: no turn files yet')

        # A: a defender at its start base (form_and_order starts with End Turn selected).
        od.use('A')
        for _ in range(4):
            ch.press('down', hold=8, after=15)
        hotseat.form_and_order(1, (140, 135))
        od.end_turn('A')                                       # file 1
        od.end_turn('B'); od.wait_log('B', 'loaded turn file 1')
        od.use('B'); ch.wait(200); hotseat.form_and_order(2, (140, 135))

        seq = 2
        while seq < 20 and 'game over' not in od.log('A'):
            sender, receiver = ('B', 'A') if seq % 2 == 0 else ('A', 'B')
            od.end_turn(sender)
            if RESTART and seq == 7:
                # B was closed after sending 6; A has now played turn 4.
                od.wait_log('A', 'turn file 7 is on the relay')
                od.start('B', game, keyB, fb, '2')
                boot_into_replay('B')
                over = watch_replay('B', 7)
                od.wait_log('B', 'map start: caught up with turn file 7; my turn')
                finish_boot('B')
            else:
                over = watch_replay(receiver, seq)
                od.wait_log(receiver, 'loaded turn file %d:' % seq)
            if over:
                overs.append((receiver, seq, over))
            if 'game over' in od.log(receiver).split('loaded turn file %d:' % seq)[-1].splitlines()[0]:
                break
            if RESTART and seq == 6:
                od.wait_log('B', 'turn file 6 is on the relay')
                od.stop('B')
            if not (RESTART and seq == 7):      # finish_boot left Tactics selected
                od.back_to_tactics(receiver)
            seq += 1
        ch.wait(300)
        for p in 'AB':
            print(p, od.shot(p, 'end-%s' % p))
    finally:
        for p in list(od.proc):
            if od.proc[p].poll() is None:
                od.stop(p)
        if relay:
            relay.terminate()

    logs = {p: '\n'.join(open(f, encoding='utf-8', errors='replace').read()
                         for f in sorted(os.listdir(OUT) and
                                         [os.path.join(OUT, x) for x in os.listdir(OUT)
                                          if x.startswith(p + '-') and x.endswith('.log')]))
            for p in 'AB'}
    battles = [l for p in 'AB' for l in logs[p].splitlines() if l.startswith('[turns] replay: battle')]
    for l in battles:
        print(' ', l)
    check('replays ran (%d)' % len(overs), len(overs) > 0)
    check('every replay ended with the units where the turn file says',
          all(' 0 unit(s) differ' in o for _, _, o in overs))
    check('a battle was replayed', len(battles) > 0)
    check('every replayed battle gave the recorded result',
          all('went exactly as recorded' in l for l in battles))
    for p in 'AB':
        check('%s: no desync' % p, 'DESYNC' not in logs[p])
    print('game over reached' if 'game over' in logs['A'] else
          'no game over (B lost its only unit in the battle; A only defends)')


if __name__ == '__main__':
    main()
