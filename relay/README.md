# Turn relay (online campaign versus)

A tiny web service that keeps the turn files of online Carnage Heart games, so two players don't need a shared folder. It is one file, `worker.js`, made to run as a free Cloudflare Worker. No player accounts: a game has a short code (like `K7QX-M2PD`), and each player gets a secret key when they create or join it.

## Setting it up (once, about 10 minutes, no programming)
Cloudflare moves its menus around now and then; if a name below differs a little, look for the closest one.
1. Make a free account at [dash.cloudflare.com](https://dash.cloudflare.com/sign-up). No card is needed for the free plan.
2. **Storage**: in the left menu open *Storage & Databases* → *KV* → *Create a namespace*. Name it `ch-turns`.
3. **The relay**: open *Compute (Workers)* → *Workers & Pages* → *Create* → *Create Worker* (the "Hello World" one). Name it `ch-turns` and press *Deploy*.
4. Press *Edit code*, delete everything in `worker.js` there, paste the whole of this folder's `worker.js`, and press *Deploy*.
5. Connect the storage: on the worker's page open *Bindings* → *Add binding* → *KV namespace*. Variable name `TURNS`, namespace `ch-turns`. Save (it redeploys).
   If creating a game later fails with `{"error":"relay error"}`, this binding is missing or has another name (Cloudflare's example code uses `KV`); it must be `TURNS`.
6. The worker's page shows its address, like `https://ch-turns.<your-name>.workers.dev`. That is the relay address the game needs. Opening it in a browser shows `{"error":"not found"}`: that means it works.

The free plan allows 1,000 stored turn files a day and 100,000 reads a day, far more than a few games need. Games are deleted 180 days after their last turn.

(Developers can deploy from a terminal instead: `npx wrangler deploy` in this folder, after putting the KV namespace id in `wrangler.toml`.)

## Telling the game where the relay is
Players use the launcher: **ONLINE PLAY → ONLINE CAMPAIGN** (NEW GAME, JOIN A GAME, PLAY). The launcher needs the relay address, from the first of these that is set:
1. the `CH_TURN_RELAY` switch;
2. the first line of `online-turns/relay.txt` beside the game (handy for testing: just paste the address in);
3. the address built into the game: by default the project's relay, `https://ch-turns.surreal64.workers.dev`; change it with `-DCH_TURN_RELAY_URL=<address>` (or `-DCH_TURN_RELAY_URL=` for none).

Each game remembers the relay it was made on, so changing the address later does not break games in progress. The launcher keeps the games and keys in `online-turns/games.txt`.

## How the game uses it
The launcher starts the game with these switches (they can also be set by hand):

| Switch | What |
|---|---|
| `CH_TURN_RELAY` | the relay address |
| `CH_TURN_GAME` | the game code |
| `CH_TURN_KEY` | this player's key |
| `CH_TURN_ROLE` | `A` (created the game) or `B` (joined it) |

The game keeps the turn files in `online-turns/<code>/` beside it. When it starts it fetches every turn file the folder lacks (so a new computer catches up), uploads its own as soon as it sends a turn, and fetches the opponent's while it waits (`src/turn_relay.c`, using the system's curl). The launcher also uploads any turn file the game could not send.

Creating and joining by hand (the launcher will do this):
```sh
curl -X POST https://<relay>/v1/games              # -> {"game":"K7QX-M2PD","player":"A","key":"..."}
curl -X POST https://<relay>/v1/games/K7QX-M2PD/join   # -> {"game":"K7QX-M2PD","player":"B","key":"..."}
curl https://<relay>/v1/games/K7QX-M2PD                # -> {"latest":7,"next":"B",...}: whose turn it is
```
The full API is described at the top of `worker.js`.

## Testing without Cloudflare
- `node relay/test.mjs`: checks the relay's rules (keys, no gaps, no rewrites, size limit).
- `node relay/dev_server.mjs 8787`: runs the relay on this PC with the storage in memory; point `CH_TURN_RELAY` at `http://127.0.0.1:8787`.
- `sh tools/research/turnsim/run.sh` includes an online game through the local relay.

Cloudflare refuses requests with Python's default User-Agent (error 1010). curl's is fine; a script talking to the relay should set its own User-Agent.
