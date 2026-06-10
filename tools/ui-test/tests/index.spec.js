// tools/ui-test/tests/index.spec.js
import { test, expect } from './_fixtures.js';

const ADMIN_TOKEN = 'a'.repeat(64);
const VIEWER_TOKEN = 'b'.repeat(64);

async function seedAuthAsAdmin(page) {
  await page.addInitScript(({ token }) => {
    sessionStorage.setItem('sauna_token', token);
  }, { token: ADMIN_TOKEN });
}
async function seedAuthAsViewer(page) {
  await page.addInitScript(({ token }) => {
    sessionStorage.setItem('sauna_token', token);
  }, { token: VIEWER_TOKEN });
}

test.describe('index.html — unauthenticated', () => {
  test('redirects to /auth/login when no token (L7)', async ({ page }) => {
    await page.goto('/');
    await page.waitForURL('**/auth/login', { timeout: 5_000 });
    await expect(page).toHaveURL(/\/auth\/login$/);
    await expect(page.locator('#u')).toBeVisible();
  });
});

test.describe('index.html — admin', () => {
  test.beforeEach(async ({ page }) => {
    await seedAuthAsAdmin(page);
  });

  test('dashboard renders sensor cards', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.authenticated')).toBeVisible();
    await expect(page.locator('h1', { hasText: 'Sauna Status' })).toBeVisible();
    // Wait for WebSocket to push the canned payload before screenshotting
    await expect(page.locator('#clt')).not.toHaveText('--', { timeout: 5_000 });
    await expect(page.locator('#d5t')).not.toHaveText('--');
    await expect(page.locator('#tct')).not.toHaveText('--');
    await page.screenshot({ path: 'screenshots/index-admin-dashboard.png', fullPage: true });
  });

  test('motor card shows separated calibrate vs move-to sections', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.authenticated')).toBeVisible();
    // C3 verification: two labeled section headers per motor
    const cal = page.getByText(/CALIBRATE — marks current position/i);
    const move = page.getByText(/MOVE TO POSITION/i);
    await expect(cal).toHaveCount(2);   // one per motor
    await expect(move).toHaveCount(2);
    // Mark vs Close button text is unambiguous
    await expect(page.getByRole('button', { name: /^Mark Closed$/ })).toHaveCount(2);
    await expect(page.getByRole('button', { name: /^Mark Open$/ })).toHaveCount(2);
    await expect(page.getByRole('button', { name: /^Close$/ })).toHaveCount(2);
    await expect(page.getByRole('button', { name: /^Open$/ })).toHaveCount(2);
    await page.screenshot({ path: 'screenshots/index-motor-cards.png', fullPage: true });
  });

  test('connection status flips to Connected after WS auth', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('#connStatus')).toHaveText('Connected', { timeout: 5_000 });
    // H3: state is signaled by class on the wrapper, not just dot color
    await expect(page.locator('#connWrapper')).toHaveClass(/\bok\b/);
    await expect(page.locator('#connWrapper')).toHaveAttribute('role', 'status');
    await expect(page.locator('#connWrapper')).toHaveAttribute('aria-live', 'polite');
  });

  test('status banner shows threshold range subtext (H4)', async ({ page }) => {
    await page.goto('/');
    // Mock WS pushes clt=175.4 + d5t=148.1 → avg ~161.75 °F → "Ready" (140–194°F)
    await expect(page.locator('#saunaStatus')).toHaveText('Ready', { timeout: 5_000 });
    await expect(page.locator('#statusRange')).toHaveText('140–194°F');
  });

  test('self-hosted chart.js + adapter load from local paths (M10)', async ({ page }) => {
    const requests = [];
    page.on('request', (req) => requests.push(req.url()));
    await page.goto('/');
    await expect(page.locator('body.authenticated')).toBeVisible();
    // Either chart bundle must come from our origin, not jsdelivr.
    const externalChartReqs = requests.filter((u) => /cdn\.jsdelivr\.net|unpkg\.com/.test(u));
    expect(externalChartReqs).toEqual([]);
    const localChart = requests.find((u) => u.endsWith('/chart.umd.min.js'));
    const localAdapter = requests.find((u) => u.endsWith('/chart-adapter.min.js'));
    expect(localChart).toBeTruthy();
    expect(localAdapter).toBeTruthy();
    // Both files must be served with a successful response and be > 10 KB.
    for (const url of [localChart, localAdapter]) {
      const resp = await page.request.get(url);
      expect(resp.status()).toBe(200);
      expect(resp.headers()['content-type']).toMatch(/javascript/);
      const body = await resp.body();
      expect(body.length).toBeGreaterThan(10_000);
    }
    // And the chart actually drew something (Chart instance assigned, canvas non-empty).
    const canvasW = await page.locator('#trendChart').evaluate((el) => el.width);
    expect(canvasW).toBeGreaterThan(0);
  });

  test('main grid uses CSS Grid layout (H6)', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.authenticated')).toBeVisible();
    const display = await page.locator('.grid').evaluate((el) => getComputedStyle(el).display);
    expect(display).toBe('grid');
    const cols = await page.locator('.grid').evaluate((el) =>
      getComputedStyle(el).gridTemplateColumns
    );
    // Should resolve to multiple track widths; "none" or "auto" would mean fallback
    expect(cols).not.toBe('none');
    expect(cols.length).toBeGreaterThan(0);
  });

  test('chart auto-refresh countdown ticks (M7)', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.authenticated')).toBeVisible();
    // Countdown text should appear within a second or two
    await expect(page.locator('#refreshCountdown')).toHaveText(/\(auto in 0:\d{2}\)/, { timeout: 3_000 });
  });

  test('themed confirm modal replaces native confirm and requires typing (M5)', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.admin')).toBeVisible();
    // Click "Reset sauna_status" — should open the themed modal (NOT native confirm)
    await page.getByRole('button', { name: 'Reset sauna_status' }).click();
    await expect(page.locator('#confirmModal')).toBeVisible();
    await expect(page.locator('#confirmTitle')).toHaveText(/Delete all data/i);
    // OK button is disabled until the bucket name is typed
    await expect(page.locator('#confirmOk')).toBeDisabled();
    await page.locator('#confirmInput').fill('sauna_status');
    await expect(page.locator('#confirmOk')).toBeEnabled();
    // Confirm and verify the delete fires
    await page.locator('#confirmOk').click();
    await expect(page.locator('#confirmModal')).toBeHidden();
    await expect(page.locator('#resetStatus')).toHaveText(/sauna_status cleared/i, { timeout: 5_000 });
  });

  test('themed confirm modal cancels on Cancel button (M5)', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.admin')).toBeVisible();
    await page.getByRole('button', { name: 'Reset sauna_control' }).click();
    await expect(page.locator('#confirmModal')).toBeVisible();
    await page.locator('#confirmCancel').click();
    await expect(page.locator('#confirmModal')).toBeHidden();
    // No status text because no request was made
    await expect(page.locator('#resetStatus')).toBeEmpty();
  });

  test('setpoint edit shows dirty badge and blocks WS overwrite (M6)', async ({ page }) => {
    await page.goto('/');
    // Wait for WS to push initial values
    await expect(page.locator('#cspInput')).toHaveValue('160', { timeout: 5_000 });
    await expect(page.locator('#cspDirty')).toBeHidden();
    // Diverge from the live value
    await page.locator('#cspInput').fill('175');
    await expect(page.locator('#cspDirty')).toBeVisible();
    // Mock pushes a new payload every 2 s; the dirty input must NOT be overwritten
    await page.waitForTimeout(2500);
    await expect(page.locator('#cspInput')).toHaveValue('175');
    // Returning to the live value clears the badge
    await page.locator('#cspInput').fill('160');
    await expect(page.locator('#cspDirty')).toBeHidden();
  });

  test('admin-required sections are visible', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('body.admin')).toBeVisible();
    // First admin-required block (outflow motor) should be displayed
    const adminBlocks = page.locator('.admin-required').first();
    await expect(adminBlocks).toBeVisible();
  });
});

test.describe('index.html — viewer', () => {
  test('admin-only blocks stay hidden for viewer role', async ({ page }) => {
    await seedAuthAsViewer(page);
    await page.goto('/');
    await expect(page.locator('body.authenticated')).toBeVisible();
    await expect(page.locator('body.admin')).toHaveCount(0);
    // Motor cards should not be visible
    await expect(page.getByRole('button', { name: /^Mark Closed$/ })).toHaveCount(0);
    await page.screenshot({ path: 'screenshots/index-viewer-dashboard.png', fullPage: true });
  });
});
