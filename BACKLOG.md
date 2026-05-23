# Backlog

## Session Checkpoint

<!-- CHECKPOINT:START -->
- Refreshed: 2026-05-22
- Branch: `master`
- Latest commit: `43b0761 refactor(ui): L6 + L7 — single login UI, addEventListener for all handlers`
- Tracked checkpoint command: `bash scripts/update-checkpoint.sh`
- Local handoff artifact: `bash scripts/update-handoff.sh` writes ignored `HANDOFF.md`
- Validation baseline:
  - `pio test -e native`  (320 native unit tests)
  - `pio run -e lb_esp32s3 -t buildprog`  (firmware compile only, no flash)
  - `python3 scripts/verify_doc_drift.py`  (routes + constants + NVS keys vs docs)
  - `cd tools/ui-test && ./node_modules/.bin/playwright test`  (44 UI integration tests, desktop + mobile)
- Current focus:
  - P1 hardware validation on a real board — the entire UI lane (BACKLOG.md "UI / Design") and the drift-checker extension are shipped; what is left needs live hardware in front of a person.
<!-- CHECKPOINT:END -->

## P1

- Validate real hardware behavior on device:
  - sensor freshness and NaN handling across PT1000, dual DHT21, and optional INA260
  - damper motor direction, limit behavior, and PID actuation under live readings
  - OTA, restart-required config changes, and LittleFS asset delivery on hardware

## P2

- Finish the documentation backlog already identified in `docs/testing.md`: undocumented routes, undocumented NVS keys, and undocumented config/auth/OTA constants.
- Add or generate route/config inventories so docs can be checked against implementation instead of drifting manually.

## P3

- ~~Split `src/web.cpp` into auth/config/OTA/WS handler files.~~ Done in `5407fe8` (2026-05-21) — `web.cpp` now 299 lines, four sibling translation units.
- Consider narrowing `refresh-docs.sh` further so AI refresh work updates only tracked docs and never depends on local-only artifacts.

## UI / Design

Findings from the 2026-05-21 design review of `data/index.html`, `data/config.html`, `data/login.html`. Ranked by impact × inverse effort. Validation note: confirm visual changes by rendering pages headlessly (see Slimmer repo for prior art) before declaring batches done.

### P1 — Safety & forward-compat (small, ship first)

- **C2** WebSocket URL hardcodes `ws://` → mixed-content block under any future HTTPS front. Two-line fix in `index.html:324`.
- **H5** `index.html` `authFetch` 401 handler does not redirect, leaving user on a half-broken page. `config.html` does redirect — bring parity.
- **C3** Motor controls collide on the word "Closed": `Set Closed` (calibration) vs `Closed` (move-to). Safety-relevant misclick. Restructure card into separate Calibration and Operation regions with unambiguous labels.

### P2 — Foundation (unlocks all later UI work)

- **H1 + M1 + M2 + M3** Lift inline styles into a CSS-custom-property design-token system and a small button-class set (`.btn`, `.btn-sm`, `.btn-xs`, `.btn-warn`, `.btn-neutral`, `.btn-stop`, `.btn-on`, `.btn-off`). Adopt modern system font stack. Unify border-radius across pages. ~60+ inline `style=` attributes in `index.html` go away.
- **H2** Add `:focus-visible` indicator on all interactive controls; remove `outline:none` reset in `config.html` or pair with replacement.
- **L1, L2, L3** `lang="en"` on `index.html`, standardize `°` glyph, normalize `<title>` format.

### P2 — Identity & accessibility

- **C1** Unify the login experience. Today: dedicated `data/login.html` (red-on-dark Sauna palette) AND an inline `#login-panel` overlay in `index.html` (Catppuccin palette). Pick one. Reconcile `sessionStorage` vs `localStorage` token storage at the same time.
- **H3** Connection state currently signaled by dot color only. Add icon/text variation for color-deficient users; have `connStatus` text track state too.
- **H4** `Idle/Warming/Ready/Hot` thresholds (86/140/194 °F) are unlabeled magic numbers. Add legend or tooltip.

### P3 — Layout & UX polish

- ~~**H6** Replace `.grid` flex-wrap with CSS Grid `auto-fit minmax(220px, 1fr)`. Add `@media` rules for sub-720 px viewports.~~ Done in `5b21ef7`.
- ~~**M4** Add client-side validation in `config.html` before submit.~~ Done in `5b21ef7`.
- ~~**M5** Replace native `confirm()` for destructive bucket resets with a themed modal; type-to-confirm for irreversible deletes.~~ Done in `36c720b`.
- ~~**M6** Mark setpoint inputs as dirty/edited so live WS updates don't silently overwrite user edits.~~ Done in `5b21ef7`.
- ~~**M7** Make trend-chart auto-refresh visible; tightened from 5 min to 60 s.~~ Done in `36c720b`.
- ~~**M8** Stove sensor null state should show `ERR` red, matching ceiling/bench.~~ Done in `5b21ef7`.
- ~~**M9** Surface real auth errors — 429 lockout distinct from 401.~~ Done in `5b21ef7`.
- ~~**M10** Self-host `chart.js` + `chartjs-adapter-date-fns` to LittleFS.~~ Done in `0ab0cfb` — added a `streamVendorJs(path)` helper + two routes (`/chart.umd.min.js`, `/chart-adapter.min.js`) in `src/web.cpp`, vendored pinned 4.4.7 + 3.0.0 bundles into `data/`.
- ~~**L4** Chart legend size/contrast.~~ Done in `36c720b` — `boxWidth: 16`, padding 14, color `#eee`, font size 13.
- ~~**L5** Login render-then-redirect flash.~~ Done in `36c720b` — `body.checking-auth .box { visibility: hidden }` until `/auth/status` resolves.
- ~~**L6** Inline `onclick=` migration to `addEventListener`.~~ Done — 27 inline handlers replaced with delegated dispatch via `data-motor`/`data-cmd`/`data-pid`/`data-reset` attributes and one-off IDs.
- ~~**L7** Redundant login-panel-vs-page logic.~~ Done — inline `#login-panel` and `doLogin()` removed; `/` now redirects to `/auth/login` when unauthenticated, leaving a single login UI.
