import { expect, test, type Page } from '@playwright/test';

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

async function waitForServiceWorker(page: Page) {
	await page.evaluate(async () => {
		await navigator.serviceWorker.ready;
	});
	// The first load is not controlled yet: reload once so the worker serves the page.
	await page.reload();
	await expect.poll(() => page.evaluate(() => !!navigator.serviceWorker.controller)).toBe(true);
}

test('OFF01-web: after one online visit the game starts and plays a full mission offline', async ({ page, context }) => {
	await page.goto('/?seed=4242');
	await expect(page.getByRole('button', { name: /Iniciar missão/ }).first()).toBeVisible();
	await waitForServiceWorker(page);

	await context.setOffline(true);
	await page.reload();
	await expect(page.getByText('OFFLINE').first()).toBeVisible();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click(); // first start shows the tutorial
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();

	// Offline reload mid-run resumes from IndexedDB.
	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	for (let i = 0; i < 80 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await answerIfAsked(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(40);
	}
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
	await expect(page.getByText(/Execução #1/).first()).toBeVisible();
	await context.setOffline(false);
});
