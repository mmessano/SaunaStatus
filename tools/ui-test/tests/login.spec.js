// tools/ui-test/tests/login.spec.js
import { test, expect } from './_fixtures.js';

test.describe('login.html', () => {
  test('renders the login form', async ({ page }) => {
    await page.goto('/auth/login');
    await expect(page).toHaveTitle(/Login/i);
    await expect(page.locator('#u')).toBeVisible();
    await expect(page.locator('#p')).toBeVisible();
    await expect(page.getByRole('button', { name: /Sign In/i })).toBeVisible();
    await expect(page.locator('#err')).toHaveText('');
    await page.screenshot({ path: 'screenshots/login-empty.png', fullPage: true });
  });

  test('rejects empty credentials client-side', async ({ page }) => {
    await page.goto('/auth/login');
    await page.getByRole('button', { name: /Sign In/i }).click();
    await expect(page.locator('#err')).toHaveText(/Enter username and password/i);
    await page.screenshot({ path: 'screenshots/login-empty-error.png', fullPage: true });
  });

  test('rejects bad credentials from server', async ({ page }) => {
    await page.goto('/auth/login');
    await page.locator('#u').fill('admin');
    await page.locator('#p').fill('wrong-password');
    await page.getByRole('button', { name: /Sign In/i }).click();
    await expect(page.locator('#err')).toHaveText(/Invalid/i, { timeout: 5_000 });
    await page.screenshot({ path: 'screenshots/login-server-rejected.png', fullPage: true });
  });

  test('admin login redirects to /', async ({ page }) => {
    await page.goto('/auth/login');
    await page.locator('#u').fill('admin');
    await page.locator('#p').fill('changeme1');
    await Promise.all([
      page.waitForURL('**/', { timeout: 5_000 }),
      page.getByRole('button', { name: /Sign In/i }).click(),
    ]);
  });

  test('429 rate-limit surfaces distinct message (M9)', async ({ page }) => {
    await page.goto('/auth/login');
    await page.locator('#u').fill('lockedout');
    await page.locator('#p').fill('anything');
    await page.getByRole('button', { name: /Sign In/i }).click();
    await expect(page.locator('#err')).toHaveText(/Too many attempts/i, { timeout: 5_000 });
    await page.screenshot({ path: 'screenshots/login-rate-limited.png', fullPage: true });
  });
});
