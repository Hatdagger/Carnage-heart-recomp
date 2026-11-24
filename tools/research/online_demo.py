"""Online campaign test with two real copies through a relay (multiplayer plan
step 6): catching up when the map opens, closing the game after sending, and
a player on a "new computer" (empty folder).

Needs a build with -DCH_REMOTE_BATTLE=ON, Node (for relay/dev_server.mjs) and
curl. Run from the repo:  python3 tools/research/online_demo.py
It starts the relay on port 8787 (or uses CH_RELAY_URL), makes a game, starts
the two copies headless from build/ with separate folders and memory cards,
and plays the hot-seat demo game (B captures A's start base). On the way it:
  1. checks both map starts (A: new game; B: waits for A's turn 1);
  2. closes B at the start of its turn and starts it again: the map start must
     catch up with A's newest turn without an End Turn, and show exactly the
     state B had before closing;
  3. does the same with an empty folder (a new computer): everything comes
     from the relay;
  4. closes B right after it sends a turn (A has not played yet) and starts it
     again: the map start loads B's own turn, and the first End Turn waits for
     A's;
  5. plays on to the end (victory on B, defeat on A).
Logs and screenshots go to build/research/online/.
"""
import json, os, shutil, signal, subprocess, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ch, hotseat

REPO = ch.REPO
BUILD = os.path.join(REPO, 'build')
OUT = os.path.join(BUILD, 'research', 'online')
EXE = os.path.join(BUILD, 'CarnageHeart_Recompiled')
DISC = os.path.join(REPO, 'disc', 'Carnage Heart (USA).cue')
PORTS = {'A': 4370, 'B': 4371}
os.environ.setdefault('no_proxy', '127.0.0.1')
os.environ.setdefault('NO_PROXY', '127.0.0.1')
RELAY = os.environ.get('CH_RELAY_URL', 'http://127.0.0.1:8787')
proc = {}
logs = {}
dirs = {}


def post(url):
    # Cloudflare refuses Python's default User-Agent (error 1010).
    req = urllib.request.Request(url, data=b'', method='POST',
                                 headers={'User-Agent': 'CarnageHeart-Test'})
    return json.load(urllib.request.urlopen(req, timeout=10))


def port_free(port, wait=30):
    """Wait until the debug port can be bound again (a copy that was just
    closed can hold it for a moment; the new copy's server then fails)."""
    import socket
    end = time.time() + wait
    while time.time() < end:
        s = socket.socket()
        try:
            s.bind(('127.0.0.1', port))
            return
        except OSError:
            time.sleep(0.5)
        finally:
            s.close()


def start(p, game, key, folder, tag):
    """Start copy p (A or B) on its own folder and memory cards."""
    port_free(PORTS[p])
    logs[p] = os.path.join(OUT, '%s-%s.log' % (p, tag))
    env = dict(os.environ, PSX_DEBUG_SERVER='1', CH_TURN_RELAY=RELAY, CH_TURN_GAME=game,
               CH_TURN_KEY=key, CH_TURN_ROLE=p, CH_TURN_DIR=folder)
    cards = os.path.join(OUT, 'card' + p)
    os.makedirs(cards, exist_ok=True)
    proc[p] = subprocess.Popen(
        [EXE, '--headless', '--no-launcher', '--memcard-dir', cards, '--disc', DISC,
         '--debug-port', str(PORTS[p])], cwd=BUILD, env=env,
        stdout=open(logs[p], 'wb'), stderr=subprocess.STDOUT)
    dirs[p] = folder
    time.sleep(6)


def stop(p):
    proc[p].send_signal(signal.SIGTERM)
    try:
        proc[p].wait(10)
    except subprocess.TimeoutExpired:
        proc[p].kill()
        proc[p].wait()


def use(p):
    ch.PORT = PORTS[p]


def log(p):
    try:
        return open(logs[p], 'rb').read().decode('utf-8', 'replace')
    except OSError:
        return ''


def wait_log(p, text, n=1, timeout=600):
    t = time.time()
    while log(p).count(text) < n:
        if time.time() - t > timeout:
            raise SystemExit('timed out waiting for %r in %s' % (text, logs[p]))
        time.sleep(0.5)


def boot(p):
    """Boot to Europa (Practice) and stop on the Command menu, Tactics selected."""
    subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'research', 'boot_europa.py'),
                    str(PORTS[p])], check=True)
    use(p)
    ch.press('circle', hold=8, after=40)


def end_turn(p):
    use(p)
    for _ in range(4):
        ch.press('down', hold=8, after=15)
    ch.press('x', hold=6, after=20)


def back_to_tactics(p):
    use(p)
    ch.wait(200)
    for _ in range(4):
        ch.press('up', hold=8, after=15)


TABLES = [(None, 0x800E017C, 64 * 22), (None, 0x800E0154, 16 * 92), (None, 0x800E014C, 200 * 12)]


def state(p):
    """Units, bases, OKEs and both funds as raw bytes (for comparing)."""
    use(p)
    import struct
    w = lambda a: struct.unpack('<I', ch.read(a, 4))[0]
    out = ch.read(0x800DF38C, 8)
    for _, ptr, n in TABLES:
        out += ch.read(w(ptr), n)
    return out


def shot(p, name):
    use(p)
    return ch.shot(os.path.join(OUT, name + '.png'))


def check(what, ok):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        raise SystemExit(1)


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    relay = None
    if 'CH_RELAY_URL' not in os.environ:
        relay = subprocess.Popen(['node', os.path.join(REPO, 'relay', 'dev_server.mjs'), '8787'],
                                 stdout=open(os.path.join(OUT, 'relay.log'), 'wb'), stderr=subprocess.STDOUT)
        time.sleep(1)
    try:
        g = post(RELAY + '/v1/games')
        game, keyA = g['game'], g['key']
        keyB = post(RELAY + '/v1/games/%s/join' % game)['key']
        print('game', game)
        fa, fb = os.path.join(OUT, 'turnsA'), os.path.join(OUT, 'turnsB')

        start('A', game, keyA, fa, '1')
        start('B', game, keyB, fb, '1')
        boot('A'); boot('B')
        wait_log('A', 'map start: no turn files yet'); wait_log('B', 'map start: no turn files yet')
        check('1. both map starts: no turn files yet (A plays turn 1, B waits)', True)

        end_turn('A')                                   # turn file 1
        end_turn('B'); wait_log('B', 'loaded turn file 1')
        use('B'); ch.wait(200); hotseat.form_and_order(2, (140, 135))
        end_turn('B'); wait_log('A', 'loaded turn file 2')   # B sends 2
        back_to_tactics('A'); end_turn('A'); wait_log('B', 'loaded turn file 3')
        back_to_tactics('B')

        # 2. Close B at the start of its turn and start it again.
        before = state('B'); shot('B', '2-before-close')
        stop('B')
        start('B', game, keyB, fb, '2')
        boot('B')
        wait_log('B', 'map start: caught up with turn file 3; my turn')
        after = state('B'); shot('B', '2-after-restart')
        check('2. B restarted: the map caught up with turn file 3 without an End Turn', after == before)

        # 3. B on a new computer: an empty folder.
        stop('B')
        fb2 = os.path.join(OUT, 'turnsB-new-computer')
        start('B', game, keyB, fb2, '3')
        boot('B')
        wait_log('B', 'caught up: the relay has 3 turn file(s)')
        wait_log('B', 'map start: caught up with turn file 3; my turn')
        after = state('B')
        check('3. B on an empty folder: fetched turn files 1-3 from the relay, same state', after == before)

        # 4. B sends its turn and closes the game before A has played.
        end_turn('B'); wait_log('B', 'turn file 4 is on the relay')
        sent = state('B')       # waiting: the state as sent (B's own view)
        stop('B')
        start('B', game, keyB, fb2, '4')
        boot('B')
        wait_log('B', 'map start: my turn file 4 is the newest')
        shot('B', '4-after-restart-their-turn')
        check('4. B closed after sending: its own turn file 4 was loaded at the map start',
              state('B') == sent)
        end_turn('B')                                   # waits for A's file 5
        wait_log('A', 'loaded turn file 4')
        back_to_tactics('A'); end_turn('A')
        wait_log('B', 'loaded turn file 5')
        back_to_tactics('B')

        # 5. Play on until B takes A's base.
        r = 5
        while 'game over' not in log('A') and r < 20:
            end_turn('B'); wait_log('A', 'loaded turn file %d' % (r + 1))
            if 'game over' in log('A'):
                break
            back_to_tactics('A'); end_turn('A'); wait_log('B', 'loaded turn file %d' % (r + 2))
            back_to_tactics('B')
            r += 2
        ch.wait(600)
        print('A', shot('A', '5-A-end'))
        use('B'); ch.wait(100)
        print('B', shot('B', '5-B-end'))
        for p in 'AB':
            check('%s: no desync' % p, 'DESYNC' not in log(p))
        check('5. game over reached', 'game over' in log('A'))
    finally:
        for p in list(proc):
            if proc[p].poll() is None:
                stop(p)
        if relay:
            relay.terminate()


if __name__ == '__main__':
    main()
