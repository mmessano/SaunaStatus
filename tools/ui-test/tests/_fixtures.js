// tools/ui-test/tests/_fixtures.js
// Shared Playwright fixture that patches window.WebSocket so the dashboard's
// hardcoded ws://<host>:81/ URL is rewritten to the mock server's WS port.
import { test as base, expect } from '@playwright/test';

const WS_PORT = Number(process.env.WS_PORT) || 18081;

export const test = base.extend({
  page: async ({ page }, use) => {
    await page.addInitScript((port) => {
      const NativeWS = window.WebSocket;
      // Wrap to rewrite :81 → :port. Use a Proxy-style override on the constructor.
      function PatchedWS(url, protocols) {
        try {
          const u = new URL(url);
          if (u.port === '81') {
            u.port = String(port);
            url = u.toString();
          }
        } catch { /* leave url as-is */ }
        return protocols ? new NativeWS(url, protocols) : new NativeWS(url);
      }
      PatchedWS.prototype = NativeWS.prototype;
      Object.defineProperty(PatchedWS, 'OPEN', { value: NativeWS.OPEN });
      Object.defineProperty(PatchedWS, 'CLOSED', { value: NativeWS.CLOSED });
      Object.defineProperty(PatchedWS, 'CONNECTING', { value: NativeWS.CONNECTING });
      Object.defineProperty(PatchedWS, 'CLOSING', { value: NativeWS.CLOSING });
      window.WebSocket = PatchedWS;
    }, WS_PORT);
    await use(page);
  },
});

export { expect };
