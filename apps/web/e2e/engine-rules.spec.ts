import { expect, test, type Page } from '@playwright/test';

// Execution policy v16 in the game: a trailing stop by percent of the close, and maintenance-margin
// tiers by position size (briefing, ticket warning and the engine's refusal).

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

async function advance(page: Page, key = 'Space') {
	await answerIfAsked(page);
	await page.keyboard.press(key);
	await page.waitForTimeout(60);
	await answerIfAsked(page);
}

async function startMission(page: Page, seed: number, index: number) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
}

test('a trailing stop by percent of the close protects a position and survives a reload', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMission(page, 31, 0);
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();

	await page.getByTestId('trail-mode-pct').click();
	const pct = page.getByTestId('protect-trail-pct');
	await pct.fill('2,5'); // PT-BR decimal comma
	await expect(page.getByTestId('protect-trail-field')).toContainText(/Gatilho inicial ≈ [\d.]+,\d{2}/);
	await page.getByRole('button', { name: /Enviar stop\/alvo/ }).click();
	const orders = page.locator('.orders');
	await expect(orders.getByText('TRAIL')).toBeVisible();
	await expect(orders.getByTestId('trail-pct')).toHaveText('2,50%');

	await advance(page);
	await page.reload(); // the action log replays the percent trail through the engine
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.orders').getByTestId('trail-pct')).toHaveText('2,50%');
	expect(errors).toEqual([]);
});

test('margin tiers: the briefing lists them and the ticket refuses a size above its tier leverage', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMission(page, 77, 2); // m03: up to 20x
	await expect(page.getByTestId('brief-tiers')).toContainText('≥ 50.000: 1,0% (até 20x)');
	await expect(page.getByTestId('brief-tiers')).toContainText('≥ 250.000: 2,5% (até 10x)');
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);

	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await lev.fill('20');
	await lev.dispatchEvent('change');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.locator('label:has-text("Quantidade") input').first().fill('3000');
	const warn = page.getByTestId('ticket-above-tier');
	await expect(warn).toBeVisible();
	await expect(warn).toContainText(/manutenção de 2,5% e alavancagem de até 10x/);
	await expect(page.getByRole('button', { name: /Enviar ordem/ })).toBeDisabled();

	await page.locator('label:has-text("Quantidade") input').first().fill('10');
	await expect(warn).toHaveCount(0);
	expect(errors).toEqual([]);
});
