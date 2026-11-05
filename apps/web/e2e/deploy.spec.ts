import { expect, test } from '@playwright/test';
import { marketOrder, playToEnd, startFirstMission, step, turn, waitForServiceWorker } from './helpers.ts';

// Safe web updates (spec web-client "Update during a mission"; task 5.6). The simulated deploy is
// server-wide, so this file runs alone in the Playwright project `deploy`, after the others.

test.afterEach(async ({ request }) => {
	await request.get('/__e2e-deploy/off');
});

test('update mid-mission: the mission continues on the current version and the update is offered after it ends', async ({ page, request }) => {
	await page.goto('/?seed=4242');
	await waitForServiceWorker(page);
	await page.evaluate(() => ((window as unknown as { __samePage: boolean }).__samePage = true));
	await startFirstMission(page);
	await marketOrder(page);
	await step(page);

	// A new version is deployed: the service worker script changes on the server (scripts/previewDeploy.ts).
	await request.get('/__e2e-deploy/next');
	await page.evaluate(async () => (await navigator.serviceWorker.getRegistration())!.update());
	await expect.poll(() => page.evaluate(async () => !!(await navigator.serviceWorker.getRegistration())?.waiting)).toBe(true);

	// Mid-mission: no prompt, no reload, the game keeps going on the running version.
	await expect(page.getByText('Nova versão disponível.')).toHaveCount(0);
	const before = await turn(page);
	await step(page);
	expect(await turn(page)).toBe(before + 1);
	await playToEnd(page);
	await expect(page.getByText('Nova versão disponível.')).toHaveCount(0);
	expect(await page.evaluate(() => (window as unknown as { __samePage?: boolean }).__samePage)).toBe(true);

	// After the mission, the menu offers it; applying reloads into the new version.
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByText('Nova versão disponível.')).toBeVisible();
	const reloaded = page.waitForEvent('load');
	await page.getByRole('button', { name: 'Atualizar agora' }).click();
	await reloaded;
	await expect(page.getByRole('button', { name: /Iniciar missão/ }).first()).toBeVisible();
	expect(await page.evaluate(() => (window as unknown as { __samePage?: boolean }).__samePage ?? false)).toBe(false);
	expect(await page.evaluate(async () => !!(await navigator.serviceWorker.getRegistration())?.waiting)).toBe(false); // now active
	await expect(page.getByText('Nova versão disponível.')).toHaveCount(0);
});
