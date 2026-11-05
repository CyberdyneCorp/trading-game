import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';

// Gameplay UX (web-client tasks 4.1a, 4.6, 4.7, 4.8, 4.9): ticket sizing modes, rejection reasons with
// their numbers, the liquidation flow, the accessible chart (UX01), remappable shortcuts, the in-game
// journal and an interrupted pack download.

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

async function startFromMenu(page: Page, index = 0) {
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
}

async function openTerminal(page: Page, seed: number, index = 0) {
	await page.goto(`/?seed=${seed}`);
	await startFromMenu(page, index);
	await expect(page.locator('canvas').first()).toBeVisible();
	await answerIfAsked(page);
}

const num = (s: string) => Number(s.replace(/[^\d,-]/g, '').replace(',', '.'));
const sizeBy = (page: Page) => page.locator('label:has-text("Dimensionar por") select');

async function sendWithoutPlan(page: Page) {
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

test('the ticket sizes by notional and by % of equity, reading PT-BR decimal commas', async ({ page }) => {
	await openTerminal(page, 4242);
	const qty = page.getByTestId('ticket-est-qty');
	const notional = page.getByTestId('ticket-est-notional');

	await sizeBy(page).selectOption('notional');
	await page.getByTestId('ticket-notional').fill('2050,5');
	const n = num((await notional.textContent())!);
	expect(n).toBeLessThanOrEqual(2050.5);
	expect(n).toBeGreaterThan(2050.5 - 200); // within one lot of the amount
	expect(num((await qty.textContent())!)).toBeGreaterThan(0);

	await sizeBy(page).selectOption('equity');
	await page.getByTestId('ticket-equity').fill('12,5'); // 12.5% of 10,000 = 1,250 of exposure
	const e = num((await notional.textContent())!);
	expect(e).toBeLessThanOrEqual(1250);
	expect(e).toBeGreaterThan(1150);

	// The order goes out at the size shown, and fills at it.
	const shown = (await qty.textContent())!.trim();
	await sendWithoutPlan(page);
	await advance(page);
	await expect(page.locator('.feed').getByText(new RegExp(`Execução #1: BUY ${shown.replace('.', '\\.')} @`))).toBeVisible();

	// Risk % also takes a comma: half the risk is about half the size.
	await sizeBy(page).selectOption('risk');
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	const one = num((await qty.textContent())!);
	await page.getByTestId('ticket-risk').fill('0,5');
	const half = num((await qty.textContent())!);
	expect(half).toBeGreaterThan(one * 0.45);
	expect(half).toBeLessThan(one * 0.55);
});

test('rejections explain themselves with numbers: margin not available, margin below the initial one', async ({ page }) => {
	await openTerminal(page, 31);
	await sizeBy(page).selectOption('equity');
	await page.getByTestId('ticket-equity').fill('90');
	await sendWithoutPlan(page);
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();

	// Adding 5.000,50 with about 1,000 available (the amount typed with a decimal comma).
	await page.getByLabel('Valor (cr)').fill('5000,50');
	await page.getByRole('button', { name: '+ margem' }).click();
	const notice = page.locator('.notice');
	await expect(notice).toContainText(/margem insuficiente: precisava de 5\.000,50 cr e havia [\d.]+,\d\d cr disponíveis/);
	// Removing it below the initial margin.
	await page.getByRole('button', { name: '− margem' }).click();
	await expect(notice).toContainText(/a margem ficaria em [\d.]+,\d\d cr, abaixo da margem inicial de [\d.]+,\d\d cr/);
});

test('an option buy whose premium fits but whose fee does not is refused without contradictory numbers', async ({ page }) => {
	await page.goto('/?seed=12');
	await startFromMenu(page, 3); // m04 calls and puts
	await answerIfAsked(page);
	await page.keyboard.press('o');
	const row = page.locator('table').filter({ hasText: 'Strike' }).last().locator('tbody tr').first();
	const ask = num((await row.locator('td').nth(3).textContent())!);
	await row.locator('td').nth(3).click();
	// The premium alone fits the 10,000 available; the engine also reserves the fee, which does not.
	const ticket = page.locator('section.oticket');
	await ticket.locator('label:has-text("Contratos") input').fill(String(Math.floor(10_000 / ask)));
	await ticket.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.locator('.notice')).toHaveText('✖ margem insuficiente');
});

test('a lower leverage that would need more margin than available is refused with both amounts', async ({ page }) => {
	await openTerminal(page, 31);
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await lev.fill('5');
	await lev.dispatchEvent('change');
	await sizeBy(page).selectOption('equity');
	await page.getByTestId('ticket-equity').fill('300');
	await sendWithoutPlan(page);
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();

	// A small add at 1x: the engine re-margins the whole position (about 30,000 at 1x) first.
	await lev.fill('1');
	await lev.dispatchEvent('change');
	await sizeBy(page).selectOption('qty');
	await page.locator('label:has-text("Quantidade") input').first().fill('1');
	await expect(page.getByTestId('relever-note')).toBeVisible();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.locator('.notice')).toContainText(/margem insuficiente: precisava de [\d.]+,\d\d cr e havia [\d.]+,\d\d cr disponíveis/);
	const text = (await page.locator('.notice').textContent())!;
	const [need, avail] = [...text.matchAll(/([\d.]+,\d\d) cr/g)].map((m) => num(m[1]));
	expect(need).toBeGreaterThan(avail);
	expect(need).toBeGreaterThan(20_000);
});

test('liquidation flow: a 20x long is liquidated, the feed and rail say so, and the debrief explains it', async ({ page }) => {
	await openTerminal(page, 3, 2); // m03 volatility shock, cap 20x
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await lev.fill('20');
	await lev.dispatchEvent('change');
	await sizeBy(page).selectOption('equity');
	await page.getByTestId('ticket-equity').fill('1500');
	await sendWithoutPlan(page);
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByText('Preço de liquidação', { exact: true })).toBeVisible();
	await expect(page.locator('.liq-price')).toHaveText(/^\d+,\d\d$/);

	const liquidation = page.locator('.feed').getByText(/LIQUIDAÇÃO @ [\d.]+,\d\d — margem perdida [\d.]+,\d\d cr/);
	for (let i = 0; i < 40 && !(await liquidation.count()); i++) await advance(page);
	await expect(liquidation).toBeVisible();
	await expect(page.getByText('Preço de liquidação', { exact: true })).toHaveCount(0); // flat: no position panel any more

	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Liquidação na barra \d+ a [\d.]+,\d\d: a margem isolada inteira/)).toBeVisible();
});

test('UX01: the bar table ends with the current closed bar, pages back, and candles take keyboard focus', async ({ page }) => {
	await openTerminal(page, 4242);
	const table = page.getByTestId('chart-table');
	const lastRow = () => table.locator('tbody tr').last().locator('th');
	const summary = page.getByTestId('chart-summary');
	const turn = async () => Number(await page.locator('.turn strong').textContent());

	const t0 = await turn();
	const last0 = Number(await lastRow().textContent());
	await expect(summary).toContainText(`Última, #${last0}:`);
	await expect(table.locator('tbody tr')).toHaveCount(50);
	await advance(page);
	expect(await turn()).toBe(t0 + 1);
	await expect(lastRow()).toHaveText(String(last0 + 1)); // the new bar, and nothing after it
	await expect(summary).toContainText(`${last0 + 2} barras fechadas. Última, #${last0 + 1}:`);

	// The table opens on screen and pages back through every closed bar.
	await page.getByTestId('chart-table-toggle').click();
	await expect(table).toBeVisible();
	const first = Number(await table.locator('tbody tr').first().locator('th').textContent());
	await page.getByTestId('table-prev').click();
	await expect(lastRow()).toHaveText(String(first - 1));
	await expect(page.getByTestId('table-range')).toContainText(`de ${last0 + 2}`);
	// Regression: reaching the first page must not drop the focus (a disabled button lost it, so Esc
	// could not return it to the toggle).
	await expect(page.getByTestId('table-prev')).toHaveAttribute('aria-disabled', 'true');
	await expect(page.getByTestId('table-prev')).toBeFocused();
	await page.keyboard.press('Escape');
	// Esc hides it even though the focus was on one of its buttons: the focus goes back to the toggle.
	await expect(table).not.toBeInViewport();
	await expect(page.getByTestId('chart-table-toggle')).toBeFocused();

	// Per-candle focus: End announces the current bar, arrows step, Space still advances.
	const chart = page.getByTestId('chart-box');
	await chart.focus();
	await page.keyboard.press('End');
	await expect(chart).toHaveAttribute('aria-valuetext', new RegExp(`^Barra #${last0 + 1}: abertura [\\d.]+,\\d\\d, máxima`));
	await page.keyboard.press('ArrowLeft');
	await expect(chart).toHaveAttribute('aria-valuetext', new RegExp(`^Barra #${last0}: `));
	await expect(chart).toHaveAttribute('aria-valuenow', String(last0));
	await page.keyboard.press('Home');
	await expect(chart).toHaveAttribute('aria-valuenow', '0');
	await page.keyboard.press('Space');
	await expect(lastRow()).toHaveText(String(last0 + 2));
});

test('shortcuts are remappable: conflicts and reserved keys are refused, the map persists and drives the game', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByTestId('open-shortcuts').click();
	const message = page.getByTestId('keys-message');

	await page.getByTestId('rebind-buy').click();
	await page.keyboard.press('s');
	await expect(message).toContainText('A tecla S já é usada por "Boleta: vender"');
	await expect(page.getByTestId('key-buy')).toHaveText('B');

	await page.getByTestId('rebind-advance').click();
	await page.keyboard.press('Enter');
	await expect(message).toContainText('A tecla Enter é reservada');
	await expect(page.getByTestId('key-advance')).toHaveText('Space');

	// Shift does not free a reserved key: on the focused chart Shift+← still moves the candle, so it
	// would both step the focus and advance the market.
	await page.getByTestId('rebind-advance').click();
	await page.keyboard.press('Shift+ArrowLeft');
	await expect(message).toContainText('A tecla Shift + ArrowLeft é reservada');
	await expect(page.getByTestId('key-advance')).toHaveText('Space');

	await page.getByTestId('rebind-buy').click();
	await page.keyboard.press('k');
	await expect(message).toContainText('"Boleta: comprar" agora usa K');

	await page.reload();
	await page.getByTestId('open-shortcuts').click();
	await expect(page.getByTestId('key-buy')).toHaveText('K');

	await startFromMenu(page);
	await answerIfAsked(page);
	const buy = page.getByRole('radio', { name: /Comprar \/ Long/ });
	await page.keyboard.press('s');
	await expect(buy).toHaveAttribute('aria-checked', 'false');
	await page.keyboard.press('b'); // no longer bound
	await expect(buy).toHaveAttribute('aria-checked', 'false');
	await page.keyboard.press('k');
	await expect(buy).toHaveAttribute('aria-checked', 'true');

	await page.keyboard.press('?');
	await expect(page.getByTestId('help-keys')).toContainText('K');
	await page.getByTestId('help-remap').click();
	await page.getByTestId('keys-reset').click();
	await page.keyboard.press('Escape');
	await expect(page.getByRole('dialog')).toHaveCount(0);
	await page.keyboard.press('s');
	await page.keyboard.press('b');
	await expect(buy).toHaveAttribute('aria-checked', 'true');
});

test('J opens this run\'s journal (modal: Space does not advance behind it); O switches to the option chain', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.keyboard.press('j');
	await expect(page.getByTestId('run-journal-empty')).toBeVisible();
	await page.keyboard.press('Escape');

	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').first().fill('Rompimento com volume');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();

	const turn = page.locator('.turn strong');
	const before = await turn.textContent();
	await page.keyboard.press('j');
	const dialog = page.getByRole('dialog', { name: 'Diário desta partida' });
	await expect(dialog.getByTestId('journal-entry')).toContainText('Rompimento com volume');
	await page.keyboard.press('Space');
	await expect(turn).toHaveText(before!);
	await page.keyboard.press('j');
	await expect(dialog).toHaveCount(0);

	await page.goto('/?seed=12');
	await startFromMenu(page, 3);
	await answerIfAsked(page);
	const optionsTab = page.getByRole('tab', { name: /Opções/ });
	await expect(optionsTab).toHaveAttribute('aria-selected', 'false');
	await page.keyboard.press('o');
	await expect(optionsTab).toHaveAttribute('aria-selected', 'true');
	await page.keyboard.press('o');
	await expect(optionsTab).toHaveAttribute('aria-selected', 'false');
});

test('a pack cut short by an interrupted download is reported as such, not as a corrupt file', async ({ page }) => {
	await page.goto('/?seed=11');
	await page.getByTestId('import-csv').click();
	let csv = 'timestamp,open,high,low,close,volume\n';
	for (let i = 0; i < 120; i++) csv += `${1_700_000_000 + i * 3600},${100 + (i % 7)},${108 + (i % 7)},${95 + (i % 7)},${101 + (i % 5)},100\n`;
	await page.getByTestId('csv-file').setInputFiles({ name: 'cut.csv', mimeType: 'text/csv', buffer: Buffer.from(csv) });
	await page.getByTestId('csv-validate').click();
	await expect(page.getByTestId('csv-report')).toContainText('Arquivo válido');
	await page.getByTestId('csv-create').click();
	await expect(page.getByTestId('pack-row')).toHaveCount(1);

	const download = page.waitForEvent('download');
	await page.getByRole('button', { name: /Exportar/ }).click();
	const bytes = readFileSync(await (await download).path());
	await page.getByRole('button', { name: 'Remover' }).click();
	await expect(page.getByTestId('pack-row')).toHaveCount(0);

	const cut = bytes.subarray(0, Math.floor(bytes.length * 0.6));
	await page.getByTestId('pack-file').setInputFiles({ name: 'cut.tlpack', mimeType: 'application/octet-stream', buffer: cut });
	await expect(page.getByTestId('pack-message')).toContainText(/Download interrompido: o arquivo está incompleto \([\d,]+ KB\)/);
	await expect(page.getByTestId('pack-row')).toHaveCount(0);

	// The whole file still installs.
	await page.getByTestId('pack-file').setInputFiles({ name: 'whole.tlpack', mimeType: 'application/octet-stream', buffer: bytes });
	await expect(page.getByTestId('pack-message')).toContainText('Pacote instalado');
});
