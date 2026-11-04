import { expect, test, type Page } from '@playwright/test';

// CSV import wizard and practice runs on imported packs (web-client task 4.5; spec scenario-packs).

/** An hourly market in millisecond timestamps around 30,000 (a real-looking BTC level). */
function csvText(rows = 120, gapAfter = -1): string {
	let out = 'timestamp,open,high,low,close,volume\n';
	let close = 30_000;
	for (let i = 0, t = 0; i < rows; i++, t++) {
		if (i === gapAfter) t += 2; // two missing hours
		const open = close;
		close = Math.round((open + (((i * 7919) % 401) - 200) * 0.5) * 100) / 100;
		const high = (Math.max(open, close) + 12).toFixed(2);
		const low = (Math.min(open, close) - 9).toFixed(2);
		out += `${1_700_000_000_000 + t * 3_600_000},${open.toFixed(2)},${high},${low},${close.toFixed(2)},${(100 + (i % 37)).toFixed(1)}\n`;
	}
	return out;
}

async function chooseCsv(page: Page, name: string, text: string) {
	await page.getByTestId('csv-file').setInputFiles({ name, mimeType: 'text/csv', buffer: Buffer.from(text) });
}

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

/** Imports the default CSV as a practice pack and returns to the menu. */
async function importPack(page: Page, gapAfter = -1) {
	await page.goto('/?seed=11');
	await page.getByTestId('import-csv').click();
	await chooseCsv(page, 'btc-1h.csv', csvText(120, gapAfter));
	await page.getByTestId('csv-pair').fill('BTC/USDT');
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-report')).toContainText('Arquivo válido');
	await page.getByTestId('csv-create').click();
	await expect(page.getByTestId('pack-row')).toHaveCount(1);
}

test('DATA01: milliseconds declared as seconds are refused with a clear error, then the fixed import validates', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('import-csv').click();
	await chooseCsv(page, 'btc-1h.csv', csvText());
	await expect(page.getByTestId('csv-unit')).toHaveValue('ms'); // guessed from the file
	await page.getByTestId('csv-unit').selectOption('s');
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-error').first()).toContainText(/linha 2.*não estão na unidade declarada/);
	await expect(page.getByTestId('csv-create')).toHaveCount(0);

	await page.getByRole('button', { name: /Voltar/ }).click();
	await page.getByTestId('csv-unit').selectOption('ms');
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-report')).toContainText('120 barras');
	await expect(page.getByTestId('csv-report')).toContainText('2023-11-14T22:13:20Z');
});

test('row errors name the line; gaps are reported, not filled', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('import-csv').click();
	const broken = csvText().replace(/\n(\d+),([\d.]+),([\d.]+),/, (_m, t, o) => `\n${t},${o},1.00,`); // high below the body
	await chooseCsv(page, 'broken.csv', broken);
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-error').first()).toContainText(/linha 2.*máxima abaixo/);

	await page.getByRole('button', { name: /Voltar/ }).click();
	await chooseCsv(page, 'gappy.csv', csvText(120, 60));
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-report')).toContainText('Arquivo válido');
	await expect(page.getByTestId('csv-report')).toContainText(/1 lacuna/);
	await expect(page.getByTestId('csv-report')).toContainText(/linha 62: 2 barra/);
});

test('an imported CSV becomes a practice mission: masked prices, no reveal until the debrief', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await importPack(page);
	await expect(page.getByTestId('pack-row')).toContainText('BTC/USDT · 1h');
	await expect(page.getByTestId('pack-row')).toContainText('CONHECIDO');

	await page.getByTestId('pack-start').click();
	await expect(page.getByText(/Mercado importado \(cenário conhecido\)/)).toBeVisible();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	await answerIfAsked(page);
	await expect(page.locator('header.top')).toContainText('CONHECIDO');
	await expect(page.locator('body')).not.toContainText('BTC/USDT'); // the reveal waits for the debrief
	await expect(page.locator('body')).not.toContainText(/30[.,]0\d\d[.,]/); // prices are masked to a base of 100

	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await answerIfAsked(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(60);
	}
	await expect(page.getByText(/Missão concluída/)).toBeVisible();
	await expect(page.getByTestId('pack-reveal')).toContainText('BTC/USDT');
	await expect(page.getByTestId('pack-reveal')).toContainText('2023-11-14T22:13:20Z');
	expect(errors).toEqual([]);
});

test('a practice run resumes after a reload by replaying it on the stored pack', async ({ page }) => {
	await importPack(page);
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	for (let i = 0; i < 3; i++) await page.keyboard.press('Space');
	await expect(page.locator('.turn')).toContainText('Barra 4');
	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toContainText('Barra 4');
	await expect(page.locator('header.top')).toContainText('CONHECIDO');
});

test('a pack file that fails verification is refused with its reason', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('pack-file').setInputFiles({ name: 'bad.tlpack', mimeType: 'application/octet-stream', buffer: Buffer.alloc(2048, 7) });
	await expect(page.getByTestId('pack-message')).toContainText(/Pacote recusado: arquivo não é um pacote válido/);
	await expect(page.getByTestId('pack-row')).toHaveCount(0);
});

test('a saved practice run whose pack was removed does not resume and says why', async ({ page }) => {
	await importPack(page);
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.keyboard.press('Space');
	await page.reload();
	await page.getByRole('button', { name: 'Remover' }).click();
	await expect(page.getByTestId('pack-row')).toHaveCount(0);
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.getByTestId('menu-alert')).toContainText('não está mais neste dispositivo');
	await expect(page.getByRole('button', { name: /Continuar partida/ })).toHaveCount(0);
});
