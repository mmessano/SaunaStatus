// tools/ui-test/tests/index.spec.js
import { test, expect } from './_fixtures.js';

const ADMIN_TOKEN = 'a'.repeat(64);
const VIEWER_TOKEN = 'b'.repeat(64);

async function seedAuthAsAdmin(page) {
  await page.addInitScript(({ token, username, role }) => {
    localStorage.setItem('token', token);
    localStorage.setItem('username', username);
    localStorage.setItem('role', role);
  }, { token: ADMIN_TOKEN, username: 'admin', role: 'admin' });
}
async function seedAuthAsViewer(page) {
  await page.addInitScript(({ token, username, role }) => {
    localStorage.setItem('token', token);
    localStorage.setItem('username', username);
    localStorage.setItem('role', role);
  }, { token: VIEWER_TOKEN, username: 'viewer1', role: 'viewer' });
}

test.describe('index.html — unauthenticated', () => {
  test('shows the in-page login panel when no token', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('#login-panel')).toBeVisible();
    await expect(page.locator('#login-user')).toBeVisible();
    await page.screenshot({ path: 'screenshots/index-login-panel.png', fullPage: true });
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
    await expect(page.locator('#dot')).toHaveClass(/on/);
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
