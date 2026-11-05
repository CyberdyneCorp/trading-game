import { expect, test, type Page } from '@playwright/test';

// Execution policy v17 in the game: continuity bars from a CSV import with filled gaps (no trading,
// shown on the chart, the market order waits for the next trading bar) and the mark-price note.

/** Hourly CSV in seconds; `gapAfter` skips two hours before that row. */
function csvText(rows: number, gapAfter: number): string {
	let out = 'timestamp,open,high,low,close,volume\n';
	let close = 30_000;
	for (let i = 0, t = 0; i < rows; i++, t++) {
		if (i === gapAfter) t += 2;
		const open = close;
		close = Math.round((open + (((i * 7919) % 401) - 200) * 0.5) * 100) / 100;
		const high = (Math.max(open, close) + 12).toFixed(2);
		const low = (Math.min(open, close) - 9).toFixed(2);
		out += `${1_700_000_000 + t * 3600},${open.toFixed(2)},${high},${low},${close.toFixed(2)},${(100 + (i % 37)).toFixed(1)}\n`;
	}
	return out;
}

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

async function advance(page: Page, n = 1) {
	for (let i = 0; i < n; i++) {
		await answerIfAsked(page);
		const turn = await page.locator('.turn').textContent();
		await page.keyboard.press('Space');
		await expect(page.locator('.turn')).not.toHaveText(turn ?? '');
	}
	await answerIfAsked(page);
}

/** 120 rows with two missing hours before row 40: bars 40 and 41 are continuity bars (warm-up 30). */
async function importGappyPack(page: Page, fill: 'skip' | 'carryForward') {
	await page.goto('/?seed=11');
	await page.getByTestId('import-csv').click();
	await page.getByTestId('csv-file').setInputFiles({ name: 'gappy.csv', mimeType: 'text/csv', buffer: Buffer.from(csvText(120, 40)) });
	await page.getByTestId('csv-pair').fill('BTC/USDT');
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-report')).toContainText(/1 lacuna/);
	await expect(page.getByTestId('csv-gap-fill')).toHaveValue('none'); // joining stays the default
	await page.getByTestId('csv-gap-fill').selectOption(fill);
	await page.getByTestId('csv-create').click();
	await expect(page.getByTestId('pack-row')).toHaveCount(1);
}

test('filled gaps: bars without trading are shown and a market order waits for the next trading bar', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await importGappyPack(page, 'skip');
	await page.getByTestId('pack-start').click();
	await expect(page.getByTestId('brief-mark')).toContainText('Margem e liquidação usam o último preço');
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await expect(page.getByTestId('continuity-legend')).toHaveCount(0); // the gap is still in the future

	await advance(page, 9); // decision at bar 39; bar 40 has no trading
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByTestId('continuity-legend')).toContainText('1 barra sem negociação');
	await expect(page.getByTestId('continuity-tag')).toHaveText('SEM NEGOCIAÇÃO'); // the readout shows the last bar
	await expect(page.getByText(/Execução #1/)).toHaveCount(0);
	await advance(page);
	await expect(page.getByTestId('continuity-legend')).toContainText('2 barras sem negociação');
	await expect(page.getByText(/Execução #1/)).toHaveCount(0);
	await advance(page); // bar 42 trades again: the order fills at its open
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByTestId('continuity-tag')).toHaveCount(0);
	await expect(page.locator('table.sr-only')).toContainText('(SEM NEGOCIAÇÃO)');
	await expect(page.getByTestId('continuity-legend')).toHaveAttribute('title', /indicadores ignoram essas barras/i);

	await page.reload(); // the action log replays on the stored pack, gaps included
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.getByTestId('continuity-legend')).toContainText('2 barras sem negociação');
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	expect(errors).toEqual([]);
});

test('filled gaps with carry forward: the chart says how indicators treat them', async ({ page }) => {
	await importGappyPack(page, 'carryForward');
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await advance(page, 11);
	await expect(page.getByTestId('continuity-legend')).toHaveAttribute('title', /repetem o último fechamento/);
});

test('built-in missions mark positions at the last price and say so in the briefing', async ({ page }) => {
	await page.goto('/?seed=5');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByTestId('brief-mark')).toContainText('último preço');
});

async function playToEnd(page: Page) {
	for (let i = 0; i < 80 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await answerIfAsked(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(40);
	}
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
}

test('regression: the debrief says filled gaps were filled, not kept as jumps', async ({ page }) => {
	await importGappyPack(page, 'skip');
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await playToEnd(page);
	await expect(page.getByTestId('reveal-gaps')).toHaveText('1 lacuna nos dados foi preenchida com barras sem negociação.');
});

test('a joined gap is still reported as kept as a jump', async ({ page }) => {
	await page.goto('/?seed=11');
	await page.getByTestId('import-csv').click();
	await page.getByTestId('csv-file').setInputFiles({ name: 'gappy.csv', mimeType: 'text/csv', buffer: Buffer.from(csvText(120, 40)) });
	await page.getByTestId('csv-pair').fill('BTC/USDT');
	await page.getByTestId('csv-validate').click();
	await page.getByTestId('csv-create').click(); // default: join the bars across the gap
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await playToEnd(page);
	await expect(page.getByTestId('reveal-gaps')).toHaveText('1 lacuna nos dados foi mantida como salto.');
});
