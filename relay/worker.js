/*
 * Carnage Heart ReComp turn relay (Cloudflare Worker, one file, no build step).
 *
 * Keeps the turn files of online campaign games (src/turn_files.c) so two
 * players don't need a shared folder. No accounts: a game has a short code,
 * and each player gets a secret key when they create or join it. Only the
 * key's owner can write that player's turn files (A the odd ones, B the even
 * ones). A turn file can't be changed once it is stored.
 *
 * Storage: one Workers KV namespace bound as TURNS (free plan is enough).
 *
 * API (all JSON except the turn file bodies):
 *   POST /v1/games                    create a game; you are player A
 *        -> {"game":"K7QX-M2PD","player":"A","key":"..."}
 *   POST /v1/games/<game>/join        join as player B (works once)
 *        -> {"game":"...","player":"B","key":"..."}
 *   GET  /v1/games/<game>             -> {"game":"...","joined":true,"latest":7,"next":"A"}
 *        ("next" is the player whose turn file is awaited)
 *   PUT  /v1/games/<game>/turns/<n>   header X-CH-Key: <key>; body = turn file n
 *   GET  /v1/games/<game>/turns/<n>   -> the turn file (404 until it exists)
 *
 * Turn files are checked lightly: at most 64 KB, they start with "CHTF",
 * their sequence number matches <n>, and n is the next number (no gaps).
 */

const MAX_TURN = 64 * 1024;
const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no 0/O, 1/I
const GAME_TTL = 60 * 60 * 24 * 180;                     // a game is kept 180 days after its last turn

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'content-type': 'application/json', 'access-control-allow-origin': '*' },
  });
}
const fail = (status, error) => json({ error }, status);

function randomCode() {
  const b = new Uint8Array(8);
  crypto.getRandomValues(b);
  let s = '';
  for (let i = 0; i < 8; i++) s += CODE_CHARS[b[i] % CODE_CHARS.length];
  return s.slice(0, 4) + '-' + s.slice(4);
}

function randomKey() {
  const b = new Uint8Array(24);
  crypto.getRandomValues(b);
  return Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
}

async function sha256(text) {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return Array.from(new Uint8Array(d), (x) => x.toString(16).padStart(2, '0')).join('');
}

// Only hashes of the keys are stored.
async function loadGame(env, game) {
  const raw = await env.TURNS.get('game:' + game);
  return raw ? JSON.parse(raw) : null;
}
async function saveGame(env, game, g) {
  await env.TURNS.put('game:' + game, JSON.stringify(g), { expirationTtl: GAME_TTL });
}

function normaliseCode(s) {
  const c = decodeURIComponent(s).toUpperCase().replace(/[^A-Z0-9]/g, '');
  return c.length === 8 ? c.slice(0, 4) + '-' + c.slice(4) : null;
}

async function handle(request, env) {
  const url = new URL(request.url);
  const parts = url.pathname.split('/').filter(Boolean); // v1 games <game> ...
  if (request.method === 'OPTIONS') {
    return new Response(null, { headers: {
      'access-control-allow-origin': '*',
      'access-control-allow-methods': 'GET, POST, PUT',
      'access-control-allow-headers': 'content-type, x-ch-key' } });
  }
  if (parts[0] !== 'v1' || parts[1] !== 'games') return fail(404, 'not found');

  // POST /v1/games
  if (parts.length === 2) {
    if (request.method !== 'POST') return fail(405, 'use POST');
    for (let tries = 0; tries < 5; tries++) {
      const game = randomCode();
      if (await loadGame(env, game)) continue;
      const key = randomKey();
      await saveGame(env, game, { created: Date.now(), keyA: await sha256(key), keyB: null, latest: 0 });
      return json({ game, player: 'A', key }, 201);
    }
    return fail(503, 'try again');
  }

  const game = normaliseCode(parts[2]);
  if (!game) return fail(400, 'bad game code');
  const g = await loadGame(env, game);
  if (!g) return fail(404, 'no such game');

  // GET /v1/games/<game>
  if (parts.length === 3) {
    if (request.method !== 'GET') return fail(405, 'use GET');
    return json({ game, joined: !!g.keyB, latest: g.latest, next: g.latest % 2 === 0 ? 'A' : 'B' });
  }

  // POST /v1/games/<game>/join
  if (parts.length === 4 && parts[3] === 'join') {
    if (request.method !== 'POST') return fail(405, 'use POST');
    if (g.keyB) return fail(409, 'this game already has two players');
    const key = randomKey();
    g.keyB = await sha256(key);
    await saveGame(env, game, g);
    return json({ game, player: 'B', key }, 201);
  }

  // /v1/games/<game>/turns/<n>
  if (parts.length === 5 && parts[3] === 'turns' && /^[0-9]{1,6}$/.test(parts[4])) {
    const n = parseInt(parts[4], 10);
    const name = 'turn:' + game + ':' + n;
    if (n < 1) return fail(400, 'turns start at 1');

    if (request.method === 'GET') {
      const body = await env.TURNS.get(name, { type: 'arrayBuffer' });
      if (!body) return fail(404, 'not yet');
      return new Response(body, { headers: {
        'content-type': 'application/octet-stream', 'access-control-allow-origin': '*' } });
    }

    if (request.method === 'PUT') {
      const key = request.headers.get('x-ch-key') || '';
      const want = n % 2 === 1 ? g.keyA : g.keyB;
      if (!want || (await sha256(key)) !== want) return fail(403, 'not your turn file');
      const body = new Uint8Array(await request.arrayBuffer());
      if (body.length < 48 || body.length > MAX_TURN) return fail(413, 'bad size');
      const magic = String.fromCharCode(body[0], body[1], body[2], body[3]);
      const seq = body[8] | (body[9] << 8) | (body[10] << 16) | (body[11] << 24);
      if (magic !== 'CHTF' || seq !== n) return fail(400, 'not turn file ' + n);
      if (n <= g.latest) {
        // Already stored: fine if it is the same file (a retried upload).
        const old = new Uint8Array((await env.TURNS.get(name, { type: 'arrayBuffer' })) || new ArrayBuffer(0));
        const same = old.length === body.length && old.every((v, i) => v === body[i]);
        return same ? json({ stored: n, latest: g.latest }) : fail(409, 'turn file ' + n + ' is already stored');
      }
      if (n !== g.latest + 1) return fail(409, 'expected turn file ' + (g.latest + 1));
      await env.TURNS.put(name, body, { expirationTtl: GAME_TTL });
      g.latest = n;
      await saveGame(env, game, g);
      return json({ stored: n, latest: n }, 201);
    }
    return fail(405, 'use GET or PUT');
  }
  return fail(404, 'not found');
}

export default {
  async fetch(request, env) {
    try {
      return await handle(request, env);
    } catch (e) {
      return fail(500, 'relay error');
    }
  },
};
