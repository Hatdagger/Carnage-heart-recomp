// Runs relay/worker.js on this PC for testing, with the KV storage in memory.
// Usage: node relay/dev_server.mjs [port]   (default 8787; Node 18 or newer)
import http from 'node:http';
import worker from './worker.js';

const store = new Map();
const env = {
  TURNS: {
    async get(k, opt) {
      if (!store.has(k)) return null;
      const v = store.get(k);
      if (opt && opt.type === 'arrayBuffer') return typeof v === 'string' ? new TextEncoder().encode(v).buffer : v;
      return typeof v === 'string' ? v : new TextDecoder().decode(v);
    },
    async put(k, v) {
      store.set(k, typeof v === 'string' ? v : v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength));
    },
  },
};

const port = Number(process.argv[2] || 8787);
http.createServer(async (req, res) => {
  const chunks = [];
  for await (const c of req) chunks.push(c);
  const body = chunks.length ? Buffer.concat(chunks) : undefined;
  const r = await worker.fetch(new Request('http://localhost' + req.url, {
    method: req.method, headers: req.headers, body: req.method === 'GET' || req.method === 'HEAD' ? undefined : body,
  }), env);
  res.writeHead(r.status, Object.fromEntries(r.headers));
  res.end(Buffer.from(await r.arrayBuffer()));
  console.log(req.method, req.url, r.status);
}).listen(port, '127.0.0.1', () => console.log('relay on http://127.0.0.1:' + port));
