import { expect, test, type Page } from '@playwright/test';

// Phase-2 batch A missions (add-scenarios-and-gameplay 6.A): each one played to its debrief, with the
// seeded reveal (regime band, signals by regime, each trade's entry regime) shown only there.

const INDEX = { m06: 5, m07: 6, m08: 7 } as const;

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

async function playToEnd(page: Page) {
	for (let i = 0; i < 60 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
}

async function startMission(page: Page, seed: number, index: number, title: RegExp, leverage: string, instrument: string) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByRole('heading', { name: title })).toBeVisible();
	await expect(page.getByText(leverage)).toBeVisible(); // the mission's own cap, from the engine
	await expect(page.getByText(instrument, { exact: true })).toBeVisible(); // its short permission, from the engine
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
}

async function buyWithPlan(page: Page, thesis: string) {
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await expect(page.getByTestId('ticket-stop').locator('input')).toHaveValue(/^\d+,\d{2}$/); // PT-BR decimal comma
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill(thesis);
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

async function expectRegimeReveal(page: Page) {
	const reveal = page.getByTestId('debrief-regimes');
	await expect(reveal).toBeVisible();
	await expect(reveal.getByTestId('regime-band').locator('[data-label]').first()).toBeVisible();
	await expect(reveal).toContainText(/Regimes desta partida, da barra 60 à \d+/);
	await expect(reveal.getByTestId('regime-signals')).toContainText(/\d+ de \d+ \(\d+%\)/); // PT-BR "3 de 7 (43%)"
}

test('m06 thin book: long only, a limit-friendly book; the debrief shows the costs and the thin phase', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMission(page, 606, INDEX.m06, /Missão 06 · Livro fino/, '1x – 2x', 'Futuro perpétuo linear (somente long)');
	await page.getByRole('radio', { name: /Vender \/ Short/ }).click();
	await expect(page.getByText('Short indisponível nesta missão')).toBeVisible();
	await page.getByRole('radio', { name: /Comprar \/ Long/ }).click();
	await buyWithPlan(page, 'Compro o suporte do range');
	await playToEnd(page);

	await expect(page.getByText(/LIVRO FINO/)).toBeVisible();
	await expect(page.getByTestId('debrief-spread')).not.toHaveText('0,00 cr');
	await expectRegimeReveal(page);
	await expect(page.getByTestId('regime-band').locator('[data-label=low_liquidity]')).toHaveCount(1);
	await expect(page.getByTestId('trade-regime').first()).toHaveText(/Lateral|Livro fino|Tendência/);
	expect(errors).toEqual([]);
});

test('m07 size and exit: a trailing stop protects the trade; the reveal names the trend', async ({ page }) => {
	await startMission(page, 707, INDEX.m07, /Missão 07 · Tamanho e saída/, '1x – 5x', 'Futuro perpétuo linear (long e short)');
	await buyWithPlan(page, 'Compro com stop pelo risco');
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/TENDÊNCIA com recuos/)).toBeVisible();
	await expectRegimeReveal(page);
	await expect(page.getByTestId('regime-band').locator('[data-label=trend]')).toHaveCount(1);
	await expect(page.getByTestId('trade-regime').first()).not.toHaveText('—');
});

test('m08 signals: the regime band and the signals-by-regime table appear only in the debrief', async ({ page }) => {
	await startMission(page, 808, INDEX.m08, /Missão 08 · Sinais/, '1x – 5x', 'Futuro perpétuo linear (long e short)');
	await expect(page.getByTestId('debrief-regimes')).toHaveCount(0); // nothing of the reveal during the run
	await buyWithPlan(page, 'Lateral: compro a sobrevenda');
	await playToEnd(page);

	await expect(page.getByText(/TROCA DE REGIME/)).toBeVisible();
	await expectRegimeReveal(page);
	const labels = await page.getByTestId('regime-band').locator('[data-label]').evaluateAll((els) => els.map((e) => e.getAttribute('data-label')));
	expect(labels.length).toBeGreaterThan(1); // the chain switched at least once in the decision window
	for (const l of labels) expect(['range', 'trend', 'high_vol']).toContain(l);
});
