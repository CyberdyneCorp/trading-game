import { expect, test, type Page } from '@playwright/test';

async function answerIfAsked(page: Page, pct?: number) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		if (pct != null) await dialog.locator('input[type=range]').fill(String(pct));
		await dialog.getByRole('button', { name: /Responder/ }).click();
	}
}

/** Advances like a player: answers calibration questions when they appear. */
async function advance(page: Page, key = 'Space') {
	await answerIfAsked(page);
	await page.keyboard.press(key);
	await page.waitForTimeout(60);
	await answerIfAsked(page);
}

async function openTerminal(page: Page, seed: number) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	await expect(page.getByRole('dialog', { name: /Pergunta de calibração/ })).toBeVisible(); // asked at the first decision
	await answerIfAsked(page);
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
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	// The planned stop is attached as a reduce-only order once the entry filled.
	await expect(page.locator('.orders').getByText('STOP')).toBeVisible();

	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await advance(page, 'Shift+Space');
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
		await advance(page, 'Shift+Space');
		await page.waitForTimeout(50);
	}
	await expect(page.getByText(/Você não operou/)).toBeVisible();
	// Risk dimension is untouched by not trading (no turnover penalty).
	const risk = page.locator('dt', { hasText: 'Risco (35%)' }).locator('xpath=following-sibling::dd[1]');
	await expect(risk).toHaveText('100');
});

test('reload mid-run resumes the same state by deterministic replay', async ({ page }) => {
	await openTerminal(page, 99);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await advance(page);
	await advance(page);
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
		for (let i = 0; i < advances; i++) await advance(page);
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
	await advance(page);
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
	await page.keyboard.press('Space'); // KEEP: raw key while the resume button may still have focus
	await expect(page.locator('.turn')).toBeVisible();
	await advance(page);
	await expect(page.locator('.orders tr')).toHaveCount(0); // the single market order filled, none duplicated
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByText(/Execução #2/)).toHaveCount(0);
	await expect(page.getByText(/replay digest mismatch/)).toHaveCount(0);
});

test('trailing stop can protect an open position and shows on the order list', async ({ page }) => {
	await openTerminal(page, 31);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await page.locator('label:has-text("Trailing stop") input').fill('2.00');
	await page.getByRole('button', { name: /Enviar stop\/alvo/ }).click();
	await expect(page.locator('.orders').getByText('TRAIL')).toBeVisible();
});

test('calibration questions are asked, cannot be skipped over, and count in the debrief', async ({ page }) => {
	await openTerminal(page, 77);
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	let asked = 1;
	for (let i = 0; i < 200 && !(await page.getByText(/Missão concluída/).count()); i++) {
		if (await dialog.count()) {
			asked++;
			await answerIfAsked(page, 50);
		}
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(40);
	}
	expect(asked).toBeGreaterThanOrEqual(5); // 79 playable bars / 15
	await expect(page.getByText(/pergunta\(s\) resolvida\(s\)/)).toBeVisible();
	await expect(page.getByText(/O que era este cenário/)).toBeVisible();
});

test('Space pressed while an order is in flight is queued, not lost', async ({ page }) => {
	await openTerminal(page, 5);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await page.keyboard.press('Space'); // immediately, while the order request may still be busy
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.locator('.turn')).toContainText('Barra 2');
});
