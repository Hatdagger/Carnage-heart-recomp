"""Turn-file test with two copies of the game on one PC (multiplayer plan step 5).

Start both copies from build/ on one shared folder, each with its own memory
cards, debug port and log. Start them a few seconds apart (two copies starting
at the same moment can trip over each other's mod state file). In a Release
build the debug server is off unless PSX_DEBUG_SERVER=1:

  PSX_DEBUG_SERVER=1 CH_TURN_DIR=research/turns CH_TURN_ROLE=A ./CarnageHeart_Recompiled \
      --no-launcher --memcard-dir research/cardA \
      --disc "../disc/Carnage Heart (USA).cue" --debug-port 4370 > research/A.log 2>&1 &
  PSX_DEBUG_SERVER=1 CH_TURN_DIR=research/turns CH_TURN_ROLE=B ./CarnageHeart_Recompiled \
      --no-launcher --memcard-dir research/cardB \
      --disc "../disc/Carnage Heart (USA).cue" --debug-port 4371 > research/B.log 2>&1 &

(add --headless to run without windows). Use a new, empty turns folder for a
new game. Bring each copy to Europa (Practice) turn 1 on the Command menu with
Tactics selected: `python3 tools/research/boot_europa.py 4370` (and 4371), then
press Circle once on each (or by hand: Start, X on Start Game, X, X on Europa;
funds 500000).

Then: python3 tools/research/turns_demo.py [rounds]
Same game as the hot-seat demo (hotseat.py demo), across two copies: A ends
turn 1; B presses End Turn once to receive it, forms a unit and sends it to
capture A's start base; then both end turns until B takes the base (game
over: victory on B, defeat on A) or the rounds run out. The logs show the
[turns] lines (sent, loaded, what the opponent did) for each copy. For a
battle, give A a unit on Defend before starting (hotseat.form_and_order(1,
(140, 135)) from the Command menu with End Turn selected).

A copy that is waiting for the other's turn file keeps running (its window
and debug server respond) and loads the file as soon as it appears.
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ch, hotseat

BUILD = os.path.join(ch.REPO, 'build')
LOGS = {'A': os.environ.get('CH_LOG_A', os.path.join(BUILD, 'research', 'A.log')),
        'B': os.environ.get('CH_LOG_B', os.path.join(BUILD, 'research', 'B.log'))}
PORTS = {'A': 4370, 'B': 4371}


def use(p):
    ch.PORT = PORTS[p]


def count(p, text):
    try:
        return open(LOGS[p], 'rb').read().count(text.encode())
    except OSError:
        return 0


def wait_log(p, text, n):
    """Wait until player p's log has the line `text` n times."""
    while count(p, text) < n:
        time.sleep(0.5)
        if count(p, 'game over') and text != 'game over':
            return False
    return True


def press_end_turn(p):
    """From the Command menu (Tactics selected): End Turn. Does not wait: the
    copy pauses at the end of the turn until the other player's file arrives."""
    use(p)
    for _ in range(4):
        ch.press('down', hold=8, after=15)
    ch.press('x', hold=6, after=20)


def back_to_tactics(p):
    """After End Turn the cursor is on End Turn: go back up to Tactics."""
    use(p)
    ch.wait(200)
    for _ in range(4):
        ch.press('up', hold=8, after=15)


def main(rounds):
    press_end_turn('A')
    # B: the first End Turn only receives A's turn 1.
    press_end_turn('B'); wait_log('B', 'loaded turn file', 1)
    # Capture A's start base (the same steps and map pick as hotseat.py demo;
    # it starts with End Turn selected and ends with Tactics selected).
    use('B'); ch.wait(200); hotseat.form_and_order(2, (140, 135))
    for r in range(rounds):
        press_end_turn('B')
        if not wait_log('A', 'loaded turn file', r + 1): break
        back_to_tactics('A')
        press_end_turn('A')
        if not wait_log('B', 'loaded turn file', r + 2): break
        back_to_tactics('B')
    for p in 'AB':
        use(p); hotseat.state(); print(p, ch.shot())


if __name__ == '__main__':
    main(int(sys.argv[1]) if sys.argv[1:] else 8)
