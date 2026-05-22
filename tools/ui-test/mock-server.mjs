// tools/ui-test/mock-server.mjs
// Fakes the SaunaStatus device for local browser testing.
//
// HTTP on $HTTP_PORT (default 18080) serves data/*.html plus stubbed JSON for
// every endpoint the UI calls. WebSocket on $WS_PORT (default 18081) accepts a
// {token: ...} auth challenge then pushes the canned sensor payload.
//
// The dashboard hardcodes ws://<host>:81/ — Playwright tests use addInitScript
// to monkey-patch the WebSocket constructor so :81 → :$WS_PORT. See
// playwright.config.js.

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';

const HTTP_PORT = Number(process.env.HTTP_PORT) || 18080;
const WS_PORT = Number(process.env.WS_PORT) || 18081;

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DATA_DIR = path.resolve(__dirname, '..', '..', 'data');
const FIXTURES = path.join(__dirname, 'fixtures');

const TOKENS = {
  admin:  'a'.repeat(64),
  viewer: 'b'.repeat(64),
};
const fixture = (name) =>
  fs.readFileSync(path.join(FIXTURES, name), 'utf8');
function roleForBearer(authHeader) {
  if (!authHeader || !authHeader.startsWith('Bearer ')) return null;
  const tok = authHeader.slice(7);
  for (const [role, t] of Object.entries(TOKENS)) if (t === tok) return role;
  return null;
}

function send(res, status, contentType, body, headers = {}) {
  res.writeHead(status, { 'Content-Type': contentType, 'Cache-Control': 'no-store', ...headers });
  res.end(body);
}
function sendJSON(res, status, obj) {
  send(res, status, 'application/json', JSON.stringify(obj));
}
function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const route = `${req.method} ${url.pathname}`;

  // ---- Static pages from LittleFS data/ ----
  const pageRoutes = {
    'GET /': 'index.html',
    'GET /config': 'config.html',
    'GET /auth/login': 'login.html',
  };
  if (pageRoutes[route]) {
    const f = path.join(DATA_DIR, pageRoutes[route]);
    try {
      const html = fs.readFileSync(f, 'utf8');
      return send(res, 200, 'text/html', html);
    } catch {
      return send(res, 500, 'text/plain', `${pageRoutes[route]} not found`);
    }
  }

  // ---- Auth ----
  if (route === 'POST /auth/login') {
    const body = await readBody(req);
    let parsed;
    try { parsed = JSON.parse(body); } catch { return sendJSON(res, 400, { error: 'bad json' }); }
    if (!parsed.username || !parsed.password) {
      return sendJSON(res, 400, { error: 'no body' });
    }
    if (parsed.username === 'admin' && parsed.password === 'changeme1') {
      return sendJSON(res, 200, {
        token: TOKENS.admin, expires_in: 3600, username: 'admin', role: 'admin',
      });
    }
    if (parsed.username === 'viewer1' && parsed.password === 'viewerpass') {
      return sendJSON(res, 200, {
        token: TOKENS.viewer, expires_in: 3600, username: 'viewer1', role: 'viewer',
      });
    }
    // Test scenario: username "lockedout" always returns 429 — lets the M9
    // distinct-lockout-message Playwright test exercise the rate-limit branch.
    if (parsed.username === 'lockedout') {
      return sendJSON(res, 429, { error: 'too many attempts, try again later' });
    }
    return sendJSON(res, 401, { error: 'invalid credentials' });
  }
  if (route === 'GET /auth/status') {
    const role = roleForBearer(req.headers.authorization);
    if (role === 'admin') {
      return sendJSON(res, 200, { valid: true, username: 'admin', role: 'admin' });
    }
    if (role === 'viewer') {
      return sendJSON(res, 200, { valid: true, username: 'viewer1', role: 'viewer' });
    }
    return sendJSON(res, 401, { error: 'token_invalid' });
  }
  if (route === 'POST /auth/logout') {
    return sendJSON(res, 200, { ok: true });
  }

  // ---- Config ----
  if (route === 'GET /config/get') {
    return send(res, 200, 'application/json', fixture('config-get.json'));
  }
  if (route === 'POST /config/save') {
    return sendJSON(res, 200, { ok: true, restart_required: false });
  }

  // ---- Operations ----
  if (route === 'POST /motor' || route === 'POST /pid' ||
      route === 'POST /setpoint' || route === 'POST /log') {
    return send(res, 200, 'text/plain', 'OK');
  }
  if (route === 'DELETE /delete/status' || route === 'DELETE /delete/control') {
    return send(res, 200, 'text/plain', 'OK');
  }

  // ---- Users (admin) ----
  if (route === 'GET /users') {
    return send(res, 200, 'application/json', fixture('users.json'));
  }
  if (route === 'POST /users') {
    return sendJSON(res, 200, { ok: true });
  }
  if (route === 'DELETE /users') {
    return sendJSON(res, 200, { ok: true });
  }
  if (route === 'PUT /users') {
    return sendJSON(res, 200, { ok: true });
  }

  // ---- History (CSV proxy) ----
  if (route === 'GET /history') {
    return send(res, 200, 'text/csv', fixture('history.csv'));
  }

  // ---- OTA ----
  if (route === 'GET /ota/status') {
    return sendJSON(res, 200, { version: '2.0.0-mock', partition: 'ota_0', boot_failures: 0 });
  }

  // ---- Fallback ----
  return send(res, 404, 'text/plain', `mock: no route for ${route}`);
});

server.listen(HTTP_PORT, () => {
  // eslint-disable-next-line no-console
  console.log(`[mock] HTTP on http://localhost:${HTTP_PORT}`);
});

// ---- WebSocket: auth challenge + canned sensor stream ----
const wss = new WebSocketServer({ port: WS_PORT });
const wsPayload = JSON.parse(fixture('ws-payload.json'));

wss.on('connection', (ws) => {
  let authed = false;
  ws.send(JSON.stringify({ auth_required: true }));

  ws.on('message', (raw) => {
    if (authed) return;
    let parsed;
    try { parsed = JSON.parse(raw.toString()); } catch {
      ws.send(JSON.stringify({ error: 'invalid_json' }));
      return ws.close();
    }
    if (parsed.token === TOKENS.admin || parsed.token === TOKENS.viewer) {
      authed = true;
      ws.send(JSON.stringify(wsPayload));
      // simulate a steady stream of broadcasts
      const t = setInterval(() => {
        if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(wsPayload));
        else clearInterval(t);
      }, 2000);
      ws.on('close', () => clearInterval(t));
    } else {
      ws.send(JSON.stringify({ error: 'token_invalid' }));
      ws.close();
    }
  });
});

// eslint-disable-next-line no-console
console.log(`[mock] WebSocket on ws://localhost:${WS_PORT}`);

// Clean shutdown for Playwright webServer lifecycle
for (const sig of ['SIGTERM', 'SIGINT']) {
  process.on(sig, () => {
    wss.close();
    server.close(() => process.exit(0));
  });
}
