// Checks relay/worker.js with in-memory storage. Usage: node relay/test.mjs
import worker from './worker.js';
import assert from 'node:assert/strict';

const store = new Map();
const env = { TURNS: {
  async get(k, opt) { const v = store.get(k); if (v == null) return null; return opt && opt.type === 'arrayBuffer' ? v : v; },
  async put(k, v) { store.set(k, typeof v === 'string' ? v : v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength)); },
} };
const call = async (method, path, { body, key } = {}) => {
  const headers = key ? { 'x-ch-key': key } : {};
  const r = await worker.fetch(new Request('http://relay' + path, { method, headers, body }), env);
  const type = r.headers.get('content-type') || '';
  return { status: r.status, data: type.includes('json') ? await r.json() : new Uint8Array(await r.arrayBuffer()) };
};
const turnFile = (seq, fill = 7) => {
  const b = new Uint8Array(200).fill(fill);
  b.set([0x43, 0x48, 0x54, 0x46], 0);
  b[8] = seq & 255; b[9] = (seq >> 8) & 255; b[10] = 0; b[11] = 0;
  return b;
};

const a = await call('POST', '/v1/games');
assert.equal(a.status, 201); assert.match(a.data.game, /^[A-Z2-9]{4}-[A-Z2-9]{4}$/);
const code = a.data.game, keyA = a.data.key;
assert.equal((await call('GET', '/v1/games/' + code)).data.joined, false);
const b = await call('POST', '/v1/games/' + code.toLowerCase().replace('-', '') + '/join'); // code typed loosely
assert.equal(b.status, 201); const keyB = b.data.key;
assert.equal((await call('POST', '/v1/games/' + code + '/join')).status, 409, 'third player refused');

assert.equal((await call('GET', `/v1/games/${code}/turns/1`)).status, 404);
assert.equal((await call('PUT', `/v1/games/${code}/turns/1`, { body: turnFile(1), key: keyB })).status, 403, 'B cannot write A');
assert.equal((await call('PUT', `/v1/games/${code}/turns/2`, { body: turnFile(2), key: keyB })).status, 409, 'no gaps');
assert.equal((await call('PUT', `/v1/games/${code}/turns/1`, { body: turnFile(2), key: keyA })).status, 400, 'seq must match');
assert.equal((await call('PUT', `/v1/games/${code}/turns/1`, { body: turnFile(1), key: keyA })).status, 201);
assert.equal((await call('PUT', `/v1/games/${code}/turns/1`, { body: turnFile(1), key: keyA })).status, 200, 'retry is fine');
assert.equal((await call('PUT', `/v1/games/${code}/turns/1`, { body: turnFile(1, 9), key: keyA })).status, 409, 'no rewrites');
const g1 = (await call('GET', `/v1/games/${code}/turns/1`)).data;
assert.deepEqual(g1, turnFile(1));
let s = (await call('GET', '/v1/games/' + code)).data;
assert.equal(s.latest, 1); assert.equal(s.next, 'B');
assert.equal((await call('PUT', `/v1/games/${code}/turns/2`, { body: turnFile(2), key: keyB })).status, 201);
s = (await call('GET', '/v1/games/' + code)).data;
assert.equal(s.latest, 2); assert.equal(s.next, 'A');
assert.equal((await call('GET', '/v1/games/ZZZZ-ZZZZ')).status, 404);
assert.equal((await call('PUT', `/v1/games/${code}/turns/3`, { body: new Uint8Array(70000), key: keyA })).status, 413);
for (const k of store.keys()) assert.ok(!String(store.get(k)).includes(keyA), 'keys are not stored');
console.log('relay: all checks passed');
