"""Negotiation in the online campaign, two real copies (build with
-DCH_REMOTE_BATTLE=ON). Starts a local relay (or uses CH_RELAY_URL), boots A
and B to Europa (Practice) headless and checks, through the game's own
Negotiation screens:

  1. A buys the CP-478FX blueprint (Kawashima Corp) on turn 1. B's copy then
     shows it as bought by the opponent ("Drakken"), not by B, and both
     copies have the same world (which blueprints exist and are in R&D).
  2. B invests in Golle 1800C1's R&D (Union Claude) on its turn 1. A's copy
     shows the investment as the opponent's; the development time it saved
     is the same on both copies.
  3. The CPU's negotiation never runs: no "funds put back" line in either
     log (it used to spend the waiting side's funds every End Turn), and the
     companies' research moves on once a round, not twice.
  4. B closes the game and starts again: its own purchases and investments
     come back from its turn file.

Logs and screenshots go to build/research/negotiation/.
"""
import os, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ch
import online_demo as od

od.OUT = os.path.join(od.BUILD, 'research', 'negotiation')
PART_STATE, RD_SLOTS, COMPANIES = 0x800DFFFC, 0x800DFF6C, 0x800E00B4
PRIVATE = 0x0002 | 0x0010 | 0x0100 | 0x0400 | 0x0800 | 0x1000 | 0x2000 | 0x8000
CP478, GOLLE = 25, 38


def w(a):
    return struct.unpack('<I', ch.read(a, 4))[0]


def parts(p):
    od.use(p)
    raw = ch.read(w(PART_STATE), 297 * 4)
    return [struct.unpack_from('<HBB', raw, 4 * i) for i in range(297)]


def slots(p):
    od.use(p)
    raw = ch.read(w(RD_SLOTS), 32 * 12)
    return [struct.unpack_from('<hxxii', raw, 12 * i) for i in range(32)]


def world(p):
    """Which blueprints exist, are on sale or in R&D (turns left, chance)."""
    out = []
    for f, b2, b3 in parts(p):
        done = f & 1
        out.append((f & ~(PRIVATE | 4), 0 if done else b2 & 63, 0 if done else b3))
    return out, [s[0] for s in slots(p)]


def negotiation(p):
    """From the Command menu, Tactics selected: open Negotiation."""
    od.use(p)
    ch.seq('down down', hold=6, after=15)
    ch.press('x', hold=8, after=120)
    ch.press('x', hold=8, after=60)      # the first visit's welcome text


def company(p, downs):
    od.use(p)
    for _ in range(downs):
        ch.press('down', hold=6, after=20)
    ch.press('x', hold=8, after=200)


def back_to_command(p, presses):
    """Circle back to the Command menu, then up to Tactics."""
    od.use(p)
    for _ in range(presses):
        ch.press('circle', hold=8, after=60)
    for _ in range(4):
        ch.press('up', hold=8, after=15)


def wait_turn_ready(p):
    od.back_to_tactics(p)


def main():
    import shutil
    shutil.rmtree(od.OUT, ignore_errors=True)
    os.makedirs(od.OUT)
    relay = None
    if 'CH_RELAY_URL' not in os.environ:
        relay = subprocess.Popen(['node', os.path.join(od.REPO, 'relay', 'dev_server.mjs'), '8787'],
                                 stdout=open(os.path.join(od.OUT, 'relay.log'), 'wb'), stderr=subprocess.STDOUT)
        time.sleep(1)
    try:
        g = od.post(od.RELAY + '/v1/games')
        game, keyA = g['game'], g['key']
        keyB = od.post(od.RELAY + '/v1/games/%s/join' % game)['key']
        fa, fb = os.path.join(od.OUT, 'turnsA'), os.path.join(od.OUT, 'turnsB')
        od.start('A', game, keyA, fa, '1')
        od.start('B', game, keyB, fb, '1')
        od.boot('A'); od.boot('B')
        od.wait_log('A', 'map start: no turn files yet'); od.wait_log('B', 'map start: no turn files yet')

        # 1. A buys CP-478FX from Kawashima Corp (company 0): Blue Prints,
        # into the list, third entry, OK.
        w0, _ = world('A')
        funds0 = (od.use('A'), w(0x800DF38C))[1]
        negotiation('A'); company('A', 0)
        od.use('A')
        ch.press('x', hold=8, after=120)                  # Blue Prints
        ch.press('right', hold=6, after=40)
        ch.seq('down down', hold=6, after=20)
        ch.press('x', hold=8, after=90)
        ch.click(200, 155, after=150)                     # OK
        od.shot('A', '1-A-bought')
        bought = parts('A')[CP478][0]
        od.check('1. A bought CP-478FX on its own copy (flag %04x, funds %d -> %d)'
                 % (bought, funds0, w(0x800DF38C)), bought & 2 and w(0x800DF38C) == funds0 - 4000)
        back_to_command('A', 4)
        od.shot('A', '1-A-command')
        od.end_turn('A')                                  # turn file 1
        od.wait_log('A', 'sent turn file 1')
        wa, sa = world('A')
        od.end_turn('B'); od.wait_log('B', 'loaded turn file 1')
        od.wait_log('B', 'negotiation and research loaded')
        wait_turn_ready('B')
        pb = parts('B')[CP478][0]
        od.check('1. B sees CP-478FX as bought by the opponent (flag %04x: Drakken\'s, not B\'s)' % pb,
                 pb & 4 and not pb & 2)
        wb, sb = world('B')
        od.check('1. both copies have the same world after turn file 1', (wa, sa) == (wb, sb))

        # 2. B invests in Golle 1800C1 at Union Claude (company 4): R&D,
        # first entry, OK (5000, the amount offered).
        turns_before = parts('B')[GOLLE][1] & 63
        negotiation('B'); company('B', 4)
        od.use('B')
        ch.press('down', hold=6, after=20)
        ch.press('x', hold=8, after=150)                  # R & D
        ch.press('x', hold=8, after=120)                  # Golle 1800C1
        ch.click(200, 155, after=150)                     # OK
        od.shot('B', '2-B-invested')
        slot = [i for i, s in enumerate(slots('B')) if s[0] == GOLLE]
        inv = slots('B')[slot[0]][1] if slot else 0
        turns_after = parts('B')[GOLLE][1] & 63
        od.check('2. B invested %d in Golle 1800C1 (development %d -> %d turns)'
                 % (inv, turns_before, turns_after), inv > 0 and turns_after < turns_before)
        back_to_command('B', 4)
        od.end_turn('B'); od.wait_log('A', 'loaded turn file 2')
        wait_turn_ready('A')
        sa2 = slots('A')[slot[0]]
        ga = parts('A')[GOLLE]
        od.check('2. A sees the investment as the opponent\'s (slot: player %d, opponent %d)'
                 % (sa2[1], sa2[2]), sa2[1] == 0 and sa2[2] == inv and not ga[0] & 0x2000)
        wa2, sa_ids = world('A')
        wb2, sb_ids = world('B')
        od.check('2. the development time B saved is on both copies (A %d, B %d turns)'
                 % (ga[1] & 63, parts('B')[GOLLE][1] & 63), (wa2, sa_ids) == (wb2, sb_ids))

        # 3. Research moves on once a round: two more rounds.
        def in_rd(ws):
            return {i: t for i, (f, t, c) in enumerate(ws) if f & 8}
        r0 = in_rd(world('A')[0])
        for n in (3, 5):
            od.end_turn('A'); od.wait_log('B', 'loaded turn file %d' % n)
            wait_turn_ready('B')
            od.end_turn('B'); od.wait_log('A', 'loaded turn file %d' % (n + 1))
            wait_turn_ready('A')
        r2 = in_rd(world('A')[0])
        steps = sorted(set(r0[i] - r2[i] for i in r2 if i in r0))
        od.check('3. research moved on once a round (2 rounds: %s turn(s) less)' % steps, steps == [2])
        od.check('3. the CPU\'s negotiation never spent the waiting side\'s funds',
                 'funds put back' not in od.log('A') and 'funds put back' not in od.log('B'))
        od.check('3. both copies still have the same world', world('A') == world('B'))

        # 4. B closes the game at the start of its turn and starts again.
        od.end_turn('A'); od.wait_log('B', 'loaded turn file 7')
        wait_turn_ready('B')
        before = (parts('B'), slots('B'), ch.read(w(COMPANIES), 48))
        od.stop('B')
        od.start('B', game, keyB, fb, '2')
        od.boot('B')
        od.wait_log('B', 'map start: caught up with turn file 7; my turn')
        after = (parts('B'), slots('B'), ch.read(w(COMPANIES), 48))
        od.check('4. B restarted: its blueprints, investments and goodwill came back', before == after)
        od.shot('B', '4-B-after-restart')
        print('all checks passed')
    finally:
        for p in list(od.proc):
            try:
                od.stop(p)
            except Exception:
                pass
        if relay:
            relay.terminate()


if __name__ == '__main__':
    main()
