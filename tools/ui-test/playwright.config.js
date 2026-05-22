// tools/ui-test/playwright.config.js
// Headless browser tests for data/*.html against the mock device server.
import { defineConfig, devices } from '@playwright/test';
import fs from 'node:fs';

const HTTP_PORT = Number(process.env.HTTP_PORT) || 18080;
const WS_PORT = Number(process.env.WS_PORT) || 18081;

// Ubuntu 26.04 currently lacks Playwright's prebuilt Chromium; fall back to
// system Chrome/Chromium. Override via PLAYWRIGHT_CHROME_PATH if needed.
const CHROME_PATH = (() => {
  if (process.env.PLAYWRIGHT_CHROME_PATH) return process.env.PLAYWRIGHT_CHROME_PATH;
  for (const p of ['/usr/bin/google-chrome', '/usr/bin/chromium-browser', '/snap/bin/chromium']) {
    if (fs.existsSync(p)) return p;
  }
  return undefined;
})();

export default defineConfig({
  testDir: './tests',
  timeout: 15_000,
  expect: { timeout: 5_000 },
  fullyParallel: false,            // shared mock server has steady WS state
  workers: 1,
  reporter: [['list'], ['html', { open: 'never', outputFolder: 'playwright-report' }]],
  outputDir: 'test-results',
  use: {
    baseURL: `http://localhost:${HTTP_PORT}`,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'off',
    // Dashboard hardcodes ws://<host>:81/ — patch at init so :81 → :WS_PORT
    // for the mock server (avoids needing root for privileged ports).
    launchOptions: {
      args: ['--disable-web-security'],
      executablePath: CHROME_PATH,
    },
  },
  projects: [
    {
      name: 'chromium-desktop',
      use: { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 800 } },
    },
    {
      name: 'chromium-mobile',
      use: { ...devices['Pixel 5'] },
    },
  ],
  webServer: {
    command: 'node mock-server.mjs',
    env: { HTTP_PORT: String(HTTP_PORT), WS_PORT: String(WS_PORT) },
    url: `http://localhost:${HTTP_PORT}/`,
    reuseExistingServer: !process.env.CI,
    timeout: 10_000,
    stdout: 'pipe',
    stderr: 'pipe',
  },
});
