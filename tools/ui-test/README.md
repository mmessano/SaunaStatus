# UI Test Harness

Headless browser tests for `data/*.html` — the LittleFS web UI. Uses a Node
mock server (`mock-server.mjs`) that fakes every HTTP and WebSocket endpoint
the dashboard, config portal, and login page call, plus Playwright to drive a
real Chromium against it.

## One-time setup

```bash
cd tools/ui-test
npm install
npm run install:browsers   # downloads Chromium for Playwright
```

## Run

```bash
cd tools/ui-test
npm test                   # headless, both desktop + mobile projects
npm run test:ui            # Playwright UI mode (interactive)
npm run test:debug         # PWDEBUG=1 — step through with inspector
npm run serve              # just run the mock server, browse http://localhost:18080/
```

Screenshots land in `tools/ui-test/screenshots/`. Trace + HTML report in
`playwright-report/` on failure.

## How the WebSocket port works

`data/index.html` hardcodes `ws://<host>:81/`. Privileged port 81 needs root,
so the mock server listens on `:18081` and a Playwright init script
(`tests/_fixtures.js`) monkey-patches `window.WebSocket` to rewrite `:81 →
:18081`. The page source is never modified.

## Fixtures

Realistic device payloads live in `fixtures/`:

- `auth-status.json` — `/auth/status` response for the seeded admin token
- `config-get.json` — `/config/get` response (mirrors the real defaults)
- `users.json` — `/users` response with one admin + one viewer
- `history.csv` — `/history` CSV from a fake 1-hour warmup run
- `ws-payload.json` — what the WebSocket pushes every 2 s (full schema)

The fake admin token is `'a'.repeat(64)` (64 chars of `a`). Login as
`admin` / `changeme1` or `viewer1` / `viewerpass`.

## What it does NOT cover

- Real ESP32-side validation (rate limiting, PBKDF2, NVS state)
- Sensor failure / staleness edge cases (mock always pushes valid readings —
  add a `?stale=ceiling` variant fixture to test that)
- OTA actual flash flow

For device-side coverage, run `pio test -e native` (320 unit tests).

## Adding a test

1. Drop a `*.spec.js` in `tests/`.
2. Import from `./_fixtures.js` (gives you the WebSocket port patch).
3. Seed `localStorage` / `sessionStorage` with the admin token via
   `page.addInitScript` if the test needs an authenticated state.
4. `npm test` to run.
