import { expect, test, type Page } from '@playwright/test';

async function openTerminal(page: Page, seed: number) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
}

test('plays a full mission: plan, order, protection, advance to debrief', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await openTerminal(page, 4242);

	await page.getByRole('button', { name: '2%?' }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await expect(page.getByRole('dialog')).toBeVisible();
	await page.locator('textarea').fill('Range: espero rompimento para cima');
	await page.getByRole('button', { name: /Registrar plano/ }).click();

	// Accepted is not filled: the feed says so before any advance.
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await page.keyboard.press('Space');
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	// The planned stop is attached as a reduce-only order once the entry filled.
	await expect(page.locator('.orders').getByText('STOP')).toBeVisible();

	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(50);
	}
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
	await expect(page.getByText(/Revelação do mercado/i)).toBeVisible();
	await expect(page.getByText(/Próxima lição recomendada/)).toBeVisible();
	expect(errors).toEqual([]);
});

test('waiting the whole mission is not penalized (EDU02)', async ({ page }) => {
	await openTerminal(page, 7);
	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída/).count()); i++) {
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(50);
	}
	await expect(page.getByText(/Você não operou/)).toBeVisible();
	await expect(page.locator('.big', { hasText: '100' })).toBeVisible();
});

test('reload mid-run resumes the same state by deterministic replay', async ({ page }) => {
	await openTerminal(page, 99);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await page.keyboard.press('Space');
	await page.keyboard.press('Space');
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	const equityBefore = await page.locator('.rail .big').innerText();
	const turnBefore = await page.locator('.turn').innerText();

	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toHaveText(turnBefore);
	await expect(page.locator('.rail .big')).toHaveText(equityBefore);
	await expect(page.getByText(/replay digest mismatch/)).toHaveCount(0);
});

test('regression: resuming twice keeps orders, leverage and progress', async ({ page }) => {
	await openTerminal(page, 2026);
	await page.locator('label:has-text("Alavancagem") input').first().fill('3');
	await page.locator('label:has-text("Tipo") select').selectOption('stop_market');
	await page.locator('label:has-text("Gatilho") input').fill('150.00');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();

	const resume = async (advances: number) => {
		for (let i = 0; i < advances; i++) await page.keyboard.press('Space');
		await page.waitForTimeout(300);
		const turn = await page.locator('.turn').innerText();
		await page.reload();
		await page.getByRole('button', { name: /Continuar partida/ }).click();
		await expect(page.locator('.turn')).toHaveText(turn);
	};
	await resume(2);
	await resume(3);
	await expect(page.locator('.orders').getByText('STOP')).toBeVisible();
	await expect(page.getByText('Sem posição · Alavancagem 3x')).toBeVisible();
	await expect(page.getByText(/replay digest mismatch/)).toHaveCount(0);
});

test('regression: a pending stop/target survives a reload and attaches on fill', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.locator('label:has-text("Stop loss") input').first().fill('90.00');
	await page.locator('label:has-text("Take profit") input').first().fill('130.00');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();

	await page.reload(); // before the market entry fills
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	await page.keyboard.press('Space');
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.locator('.orders').getByText('STOP')).toBeVisible();
	await expect(page.locator('.orders').getByText('TP', { exact: true })).toBeVisible();
});

test('regression: Space during resume does not replay the run twice', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await page.reload();
	const resume = page.getByRole('button', { name: /Continuar partida/ });
	await resume.click();
	await page.keyboard.press('Space'); // focus may still be on the resume button mid-replay
	await expect(page.locator('.turn')).toBeVisible();
	await page.keyboard.press('Space');
	await expect(page.locator('.orders tr')).toHaveCount(0); // the single market order filled, none duplicated
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByText(/Execução #2/)).toHaveCount(0);
	await expect(page.getByText(/replay digest mismatch/)).toHaveCount(0);
});
