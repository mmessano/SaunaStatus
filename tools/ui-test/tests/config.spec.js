// tools/ui-test/tests/config.spec.js
import { test, expect } from './_fixtures.js';

const FAKE_TOKEN = 'a'.repeat(64);

async function seedAuthAsAdmin(page) {
  await page.addInitScript((token) => {
    sessionStorage.setItem('sauna_token', token);
  }, FAKE_TOKEN);
}

test.describe('config.html', () => {
  test('redirects to /auth/login when no token', async ({ page }) => {
    await page.goto('/config');
    await page.waitForURL('**/auth/login');
    await expect(page).toHaveURL(/\/auth\/login/);
  });

  test('loads current config and renders form', async ({ page }) => {
    await seedAuthAsAdmin(page);
    await page.goto('/config');
    await expect(page.locator('h1')).toHaveText(/Sauna Configuration/i);
    await expect(page.locator('input[name="ceiling_setpoint_f"]')).toHaveValue('160');
    await expect(page.locator('input[name="bench_setpoint_f"]')).toHaveValue('120');
    await expect(page.locator('input[name="sensor_read_interval_ms"]')).toHaveValue('2000');
    await expect(page.locator('input[name="device_name"]')).toHaveValue('ESP32');
    await page.screenshot({ path: 'screenshots/config-loaded.png', fullPage: true });
  });

  test('submits and reports success', async ({ page }) => {
    await seedAuthAsAdmin(page);
    await page.goto('/config');
    await expect(page.locator('input[name="ceiling_setpoint_f"]')).toHaveValue('160');
    await page.getByRole('button', { name: /Save Settings/i }).click();
    await expect(page.locator('#status')).toHaveText(/Settings saved successfully/i);
    await page.screenshot({ path: 'screenshots/config-saved.png', fullPage: true });
  });
});
