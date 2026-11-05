import { expect, test, type Page } from '@playwright/test';

async function answerIfAsked(page: Page, pct?: number) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		if (pct != null) await dialog.locator('input[type=range]').fill(String(pct));
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden(); // the answer goes through the engine asynchronously
	}
}

/** Advances like a player: answers calibration questions when they appear. */
async function advance(page: Page, key = 'Space') {
	await answerIfAsked(page);
	await page.keyboard.press(key);
	await page.waitForTimeout(60);
	await answerIfAsked(page);
}

/** Starts a mission from the menu: skips its tutorial (shown on first start) and opens the terminal. */
async function startFromMenu(page: Page, index = 0) {
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
}

async function openTerminal(page: Page, seed: number) {
	await page.goto(`/?seed=${seed}`);
	await startFromMenu(page);
	await expect(page.locator('canvas').first()).toBeVisible();
	await expect(page.getByRole('dialog', { name: /Pergunta de calibração/ })).toBeVisible(); // asked at the first decision
	await answerIfAsked(page);
}

test('plays a full mission: plan, order, protection, advance to debrief', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await openTerminal(page, 4242);

	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
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

test('a mission played to the end is marked Completed in the menu, also after a reload', async ({ page }) => {
	await openTerminal(page, 7);
	const cards = page.getByTestId('mission-card');
	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída/).count()); i++) await advance(page, 'Shift+Space');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(cards.first().getByText('✓ Concluída')).toBeVisible();
	await expect(cards.nth(1).getByText(/Concluída/)).toHaveCount(0);
	await page.reload();
	await expect(cards.first().getByText('✓ Concluída')).toBeVisible();
	await expect(page.getByText(/✓ Concluída/)).toHaveCount(1);
});

/** Pixel rows of the chart's draggable stop/target tags, found by hovering down the price axis. */
async function grabbableRows(page: Page): Promise<{ x: number; ys: number[] }> {
	const box = page.getByTestId('chart-box');
	const r = (await box.boundingBox())!;
	const x = r.x + r.width - 20;
	const ys: number[] = [];
	for (let y = r.y + 4; y < r.y + r.height * 0.6; y += 3) {
		await page.mouse.move(x, y);
		if (await box.evaluate((el) => el.classList.contains('grab'))) {
			if (!ys.length || y - ys[ys.length - 1] > 12) ys.push(y);
		}
	}
	return { x, ys };
}

async function dragY(page: Page, x: number, from: number, to: number) {
	await page.mouse.move(x, from);
	await page.mouse.down();
	await page.mouse.move(x, (from + to) / 2, { steps: 4 });
	await page.mouse.move(x, to, { steps: 4 });
	await page.mouse.up();
}

test('dragging the SL? line on the chart edits the ticket stop (and Esc puts it back)', async ({ page }) => {
	await openTerminal(page, 4242);
	const stopField = page.getByTestId('ticket-stop').locator('input');
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2×ATR', exact: true }).click();
	const before = Number(await stopField.inputValue());
	const { x, ys } = await grabbableRows(page);
	expect(ys).toHaveLength(1); // only SL? is draggable (ENTRY?/LIQ? are not)
	await dragY(page, x, ys[0], ys[0] + 40);
	const after = Number(await stopField.inputValue());
	expect(after).toBeLessThan(before);

	await page.mouse.move(x, ys[0] + 40);
	await page.mouse.down();
	await page.mouse.move(x, ys[0] + 90, { steps: 4 });
	await page.keyboard.press('Escape');
	await page.mouse.up();
	expect(Number(await stopField.inputValue())).toBe(after);
});

test('dragging a working STOP line moves the order; the wrong side of the price is refused', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2×ATR', exact: true }).click();
	await page.getByTestId('ticket-tp').getByRole('button', { name: '2R', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	const orders = page.locator('.orders');
	await expect(orders.getByText('STOP')).toBeVisible();
	const stopRow = orders.locator('tr', { hasText: 'STOP' });
	const oldId = (await stopRow.locator('td').first().textContent())!.trim();
	const oldPrice = Number((await stopRow.locator('td.num').nth(1).textContent())!.replace(/\./g, '').replace(',', '.'));

	const { x, ys } = await grabbableRows(page);
	expect(ys).toHaveLength(2); // STOP and TP
	const stopY = Math.max(...ys);
	await dragY(page, x, stopY, stopY + 30);
	await expect(page.locator('.notice')).toContainText(/movida para/);
	// Regression: the bracket stop is moved in place, keeping its OCO link (before, a plain stop
	// replaced it and the TP still said OCO without a sibling).
	await expect(orders.getByText(oldId, { exact: true })).toBeVisible();
	await expect(orders.locator('tr')).toHaveCount(2);
	await expect(stopRow).toContainText('OCO');
	await expect(orders.locator('tr', { hasText: 'TP' })).toContainText('OCO');
	const newPrice = Number((await stopRow.locator('td.num').nth(1).textContent())!.replace(/\./g, '').replace(',', '.'));
	expect(newPrice).toBeLessThan(oldPrice);
	await expect(page.locator('.feed').getByText(new RegExp(`Ordem ${oldId} movida para`))).toBeVisible();

	// Above the current price a sell stop would trigger at once: refused, the order stays.
	const again = await grabbableRows(page);
	await dragY(page, x, Math.max(...again.ys), Math.min(...again.ys) - 10);
	await expect(page.getByText(/dispararia na próxima abertura/)).toBeVisible();
	await expect(orders.getByText(oldId, { exact: true })).toBeVisible();
});

test('regression: Space advances after toggling an indicator or choosing an oscillator', async ({ page }) => {
	// Before: the focused checkbox/select kept Space, so BB20 flickered and the market never moved.
	await openTerminal(page, 9);
	const bar = page.getByText(/Barra \d+ de/);
	const before = await bar.textContent();
	await page.getByLabel('BB20').check();
	await page.keyboard.press('Space');
	await expect(bar).not.toHaveText(before!);
	await expect(page.getByLabel('BB20')).toBeChecked();
	const mid = await bar.textContent();
	await page.getByTestId('osc-select').selectOption('adx');
	await page.keyboard.press('Space');
	await expect(bar).not.toHaveText(mid!);
	await expect(page.getByTestId('osc-select')).toHaveValue('adx');
});

test('the ticket warns that a new leverage re-margins the open position', async ({ page }) => {
	await openTerminal(page, 31);
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await lev.fill('2');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByTestId('relever-note')).toHaveCount(0);
	await lev.fill('2'); // same leverage: nothing to say
	await expect(page.getByTestId('relever-note')).toHaveCount(0);
	await lev.fill('5'); // m01 caps at 5x
	await lev.dispatchEvent('change');
	const note = page.getByTestId('relever-note');
	await expect(note).toContainText('2x → 5x');
	await expect(note).toContainText(/a liquidação dela vai de [\d,]+ para [\d,]+/);
	await expect(note).toHaveClass(/warnbox/); // liquidation moves closer
});

test('the option ticket refuses a buy whose premium exceeds the available balance', async ({ page }) => {
	await page.goto('/?seed=12');
	await startFromMenu(page, 3);
	await answerIfAsked(page);
	await page.locator('.chain table').first().locator('tbody tr').nth(2).locator('td').nth(3).getByRole('button').click();
	const qty = page.locator('section.oticket label', { hasText: 'Contratos' }).locator('input');
	await qty.fill('100000');
	await expect(page.getByTestId('opt-cant-afford')).toContainText(/saldo disponível/);
	await expect(page.getByRole('button', { name: /Enviar ordem de opção/ })).toBeDisabled();
	await qty.fill('10');
	await expect(page.getByTestId('opt-cant-afford')).toHaveCount(0);
	await expect(page.getByRole('button', { name: /Enviar ordem de opção/ })).toBeEnabled();
});

test('a mission tutorial is shown on first start only, can be paged, skipped and replayed from the briefing', async ({ page }) => {
	await page.goto('/?seed=5');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await expect(page.getByTestId('tut-step')).toHaveText('1 de 5');
	await expect(page.getByRole('heading', { name: 'Leia o gráfico' })).toBeVisible();
	await page.getByRole('button', { name: /Próximo/ }).click();
	await expect(page.getByRole('heading', { name: 'Long ou short' })).toBeVisible();
	await page.keyboard.press('ArrowRight');
	await expect(page.getByTestId('tut-step')).toHaveText('3 de 5');
	await page.keyboard.press('ArrowLeft');
	await expect(page.getByTestId('tut-step')).toHaveText('2 de 5');
	for (let i = 0; i < 3; i++) await page.getByRole('button', { name: /Próximo/ }).click();
	await page.getByRole('button', { name: /Ir para o briefing/ }).click();
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();

	// Replay from the briefing; Esc skips back to it.
	await page.getByRole('button', { name: /Tutorial/ }).click();
	await expect(page.getByTestId('tut-step')).toHaveText('1 de 5');
	await page.keyboard.press('Escape');
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();

	// Seen on this device: the next start (also after a reload) goes straight to the briefing.
	await page.reload();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();
	await expect(page.getByTestId('tut-step')).toHaveCount(0);
	// Other missions still show theirs.
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(1).click();
	await expect(page.getByRole('heading', { name: 'Funding' })).toBeVisible();
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
	await page.getByLabel('Stop loss').first().fill('90.00');
	await page.getByLabel('Take profit').first().fill('130.00');
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

test('regression: after a fill the ticket drops the old stop/target, and the feed names the bracket legs', async ({ page }) => {
	// Before: the next order was sized to the previous trade's stop, and legs read "LMT/STOP/TP".
	await openTerminal(page, 4242);
	await page.getByLabel('Stop loss').first().fill('90.00');
	await page.getByLabel('Take profit').first().fill('130.00');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByLabel('Stop loss').first()).toHaveValue('');
	await expect(page.getByLabel('Take profit').first()).toHaveValue('');
	const feed = page.locator('.feed');
	await expect(feed.getByText(/\(STOP venda/)).toBeVisible();
	await expect(feed.getByText(/\(TP venda/)).toBeVisible();
	await expect(feed.getByText(/LMT\/STOP\/TP/)).toHaveCount(0);
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
	await page.getByLabel(/Trailing stop/).fill('2.00');
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
	await expect(page.getByText(/\d+ perguntas? resolvidas? · Brier/)).toBeVisible();
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

test('options mission: buy a call from the chain with a plan, hold to expiry, see settlement', async ({ page }) => {
	await page.goto('/?seed=12');
	await startFromMenu(page, 3);
	await answerIfAsked(page);
	await expect(page.getByText(/PRÊMIOS SIMULADOS/).first()).toBeVisible();
	await page.locator('.chain table').first().locator('tbody tr').nth(2).locator('td').nth(3).getByRole('button').click();
	await expect(page.getByText(/Perda máxima \(prêmio\)/)).toBeVisible();
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.locator('textarea').first().fill('Compra de call antes de possível rompimento');
	await page.locator('textarea').nth(1).fill('Perde se não romper antes do vencimento ou se a IV cair');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	for (let i = 0; i < 60 && !(await page.getByText(/Vencimento C 100 · exp/).count()); i++) await advance(page, 'Shift+Space');
	// The contract is named, not shown as an internal series id.
	await expect(page.getByText(/Vencimento C 100 · exp/).first()).toBeVisible();
	await expect(page.locator('.feed').getByText(/OPT#/)).toHaveCount(0);
});

test('regression: an options trade is listed in the debrief (was "you did not trade")', async ({ page }) => {
	await page.goto('/?seed=12');
	await startFromMenu(page, 3);
	await answerIfAsked(page);
	await page.locator('.chain table').first().locator('tbody tr').nth(2).locator('td').nth(6).getByRole('button').click();
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída/)).toBeVisible();
	await expect(page.getByText(/COMPRA P 100/)).toBeVisible();
	await expect(page.locator('.trades').getByText('vencimento')).toBeVisible();
	await expect(page.getByText(/Você não operou/)).toHaveCount(0);
	// Option P&L by Greek, computed by the engine and labeled approximate.
	const greeks = page.getByTestId('debrief-greeks');
	await expect(greeks.getByText(/P&L das opções por grega \(aproximado\)/)).toBeVisible();
	for (const name of ['Delta (direção)', 'Gama (convexidade)', 'Theta (tempo)', 'Vega (vol. implícita)', 'Resíduo (spread e outros)']) {
		await expect(greeks.getByText(name)).toBeVisible();
	}
	await expect(greeks.getByText(/-?[\d.]+,\d\d cr/).first()).toBeVisible(); // PT-BR numbers
	await expect(page.getByTestId('debrief-liq-distance')).toHaveText('—'); // never held the perpetual
});

test('debrief: spread/slippage, turnover, closest liquidation and the leverage that keeps the stop in front', async ({ page }) => {
	await openTerminal(page, 31);
	await page.locator('label:has-text("Alavancagem") input').first().fill('5');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	const stop = page.getByTestId('ticket-stop');
	await stop.getByRole('button', { name: '2%', exact: true }).click();
	const near = Number(await stop.locator('input').inputValue()); // 2% under the entry
	await stop.locator('input').fill(((near / 0.98) * 0.7).toFixed(2)); // 30% under: beyond a 5x liquidation
	await expect(page.getByText(/O stop está além do preço de liquidação/)).toBeVisible();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
	const spread = page.getByTestId('debrief-spread');
	await expect(spread).toHaveText(/^[\d.]+,\d\d cr$/);
	await expect(spread).not.toHaveText('0,00 cr'); // paid on the market entry
	await expect(page.getByTestId('debrief-turnover')).toHaveText(/^[1-9][\d.]*,\d\d cr$/);
	await expect(page.getByTestId('debrief-liq-distance')).toHaveText(/^\d+,\d\d%$/);
	await expect(page.locator('.trades .advice')).toContainText(/além da liquidação [\d.]+,\d\d a 5x: até [1-4]x o manteria na frente/);
	await expect(page.getByText(/Com até [1-4]x de alavancagem o stop teria ficado na frente/)).toBeVisible();
	await expect(page.getByText(/Futuros II/)).toBeVisible();
});

test('isolated margin can be added and removed only down to the initial margin', async ({ page }) => {
	await openTerminal(page, 31);
	await page.locator('label:has-text("Alavancagem") input').first().fill('5');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await page.getByRole('button', { name: '+ margem' }).click();
	await expect(page.getByText(/Margem isolada ajustada: \+100,00 cr/)).toBeVisible();
	await page.getByRole('button', { name: '− margem' }).click();
	await page.getByRole('button', { name: '− margem' }).click();
	await expect(page.getByText(/margem ficaria abaixo da inicial/)).toBeVisible(); // reason codes are translated
});

test('news appear only once released (no lookahead), with fictional headlines', async ({ page }) => {
	await openTerminal(page, 4);
	await expect(page.getByText('Nenhuma notícia divulgada.')).toBeVisible();
	const rumor = page.getByText(/gestora fictícia Arbor teria comprado/i);
	// The rumor's bar depends on the seed (the range lasts 70-82 bars): advance until it is published.
	for (let i = 0; i < 24 && !(await rumor.count()); i++) await advance(page);
	await expect(rumor).toBeVisible();
	await expect(page.getByText(/nega rumor/i)).toHaveCount(0); // published 10 bars later: still the future
});

test('stop and target presets: ATR stop, R-multiple target, distance and reward/risk shown', async ({ page }) => {
	await openTerminal(page, 4242);
	const stop = page.getByTestId('ticket-stop');
	const tp = page.getByTestId('ticket-tp');
	await stop.getByRole('button', { name: '2×ATR' }).click();
	const stopValue = Number(await stop.locator('input').inputValue());
	expect(stopValue).toBeGreaterThan(0);
	await expect(stop.getByText(/2,0×ATR/)).toBeVisible(); // PT-BR decimal comma
	await tp.getByRole('button', { name: '2R', exact: true }).click();
	expect(Number(await tp.locator('input').inputValue())).toBeGreaterThan(stopValue);
	await expect(tp.getByText(/R:R 1:2,0/)).toBeVisible();
	// A stop on the wrong side is flagged and blocks submission.
	await stop.locator('input').fill('999');
	await expect(stop.getByText(/O stop precisa ficar </)).toBeVisible();
	await expect(page.getByRole('button', { name: /Enviar ordem/ })).toBeDisabled();
});

test('a stop can be marked on the chart; Esc cancels picking', async ({ page }) => {
	await openTerminal(page, 4242);
	const stop = page.getByTestId('ticket-stop');
	await stop.getByRole('button', { name: 'Marcar no gráfico', exact: true }).click();
	await expect(page.getByText(/Clique no gráfico para marcar/)).toBeVisible();
	await page.keyboard.press('Escape');
	await expect(page.getByText(/Clique no gráfico para marcar/)).toHaveCount(0);
	await expect(stop.locator('input')).toHaveValue('');

	await stop.getByRole('button', { name: 'Marcar no gráfico', exact: true }).click();
	const canvas = page.locator('.canvas-box canvas');
	const box = (await canvas.boundingBox())!;
	await canvas.click({ position: { x: box.width / 3, y: box.height * 0.5 } });
	await expect(page.getByText(/Clique no gráfico para marcar/)).toHaveCount(0);
	expect(Number(await stop.locator('input').inputValue())).toBeGreaterThan(0);
	// Space must not advance while a pick is pending, and does again afterwards.
	const bar = await page.getByText(/Barra \d+ de/).textContent();
	await stop.getByRole('button', { name: 'Marcar no gráfico', exact: true }).click();
	await page.keyboard.press('Space');
	await expect(page.getByText(/Barra \d+ de/)).toHaveText(bar!);
});

test('oscillator pane switches between RSI, MACD and ADX; VWAP overlay toggles', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await openTerminal(page, 4242);
	const osc = page.getByTestId('osc-select');
	await osc.selectOption('macd');
	await expect(page.locator('.readout').getByText(/^MACD -?\d/)).toBeVisible();
	await osc.selectOption('adx');
	await expect(page.locator('.readout').getByText(/ADX \d+,\d · \+DI/)).toBeVisible();
	await osc.selectOption('none');
	await page.getByLabel('VWAP').check();
	await expect(page.locator('.readout').getByText(/VWAP \d/)).toBeVisible();
	expect(errors).toEqual([]);
});

test('an open position without a stop is flagged until protected', async ({ page }) => {
	await openTerminal(page, 31);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByText(/Posição sem stop/)).toBeVisible();
	await page.getByTestId('protect-stop').getByRole('button', { name: '1×ATR' }).click();
	await page.getByRole('button', { name: /Enviar stop\/alvo/ }).click();
	await expect(page.locator('.orders').getByText('STOP')).toBeVisible();
	await expect(page.getByText(/Posição sem stop/)).toHaveCount(0);
});

test('candle pattern cheat sheet opens with P, filters by bias, and blocks Space behind it', async ({ page }) => {
	await openTerminal(page, 4242);
	const bar = await page.getByText(/Barra \d+ de/).textContent();
	await page.keyboard.press('p');
	const sheet = page.getByRole('dialog', { name: 'Padrões de candle' });
	await expect(sheet).toBeVisible();
	await expect(sheet.getByTestId('pattern-hammer')).toBeVisible();
	await sheet.getByRole('radio', { name: 'Baixa' }).click();
	await expect(sheet.getByTestId('pattern-shooting-star')).toBeVisible();
	await expect(sheet.getByTestId('pattern-hammer')).toHaveCount(0);
	await page.keyboard.press('Space');
	await expect(page.getByText(/Barra \d+ de/)).toHaveText(bar!);
	await page.keyboard.press('Escape');
	await expect(sheet).toHaveCount(0);
	await page.getByRole('button', { name: /Padrões/ }).click();
	await expect(sheet).toBeVisible();
});

test('cheat sheet animates each candle pattern: confirmation and follow-through, then the failure', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.keyboard.press('p');
	const card = page.getByRole('dialog', { name: 'Padrões de candle' }).getByTestId('pattern-hammer');
	const anim = card.locator('figure.anim');
	await expect(anim).toHaveAttribute('data-stage', 'confirmed', { timeout: 3000 });
	await expect(card.getByText('✓ Confirmou')).toBeVisible();
	await expect(anim.locator('g.next.first')).toHaveCount(1); // the confirmation candle
	await expect(anim.locator('.level.trigger')).toHaveCount(1);
	await expect(anim).toHaveAttribute('data-stage', 'failed', { timeout: 6000 });
	await expect(card.getByText('✗ Falhou: stop')).toBeVisible();
	await expect(anim.locator('.level.stop')).toHaveCount(1);
	const doji = page.getByTestId('pattern-doji').locator('figure.anim');
	await expect(doji).toHaveAttribute('data-stage', /breakUp|breakDown|pattern/);
});

test('with reduced motion the cheat sheet shows the textbook outcome as a still picture', async ({ page }) => {
	await page.emulateMedia({ reducedMotion: 'reduce' });
	await openTerminal(page, 4242);
	await page.keyboard.press('p');
	const anim = page.getByTestId('pattern-hammer').locator('figure.anim');
	await expect(anim).toHaveAttribute('data-stage', 'confirmed');
	await expect(anim.locator('g.next')).toHaveCount(3);
	await page.waitForTimeout(1500);
	await expect(anim).toHaveAttribute('data-stage', 'confirmed');
});

test('regression: Space does not advance behind the help overlay', async ({ page }) => {
	await openTerminal(page, 4243);
	const bar = await page.getByText(/Barra \d+ de/).textContent();
	await page.keyboard.press('?');
	const help = page.getByRole('dialog', { name: 'Atalhos' });
	await expect(help).toBeVisible();
	await page.keyboard.press('Space');
	await page.keyboard.press('b');
	await expect(page.getByText(/Barra \d+ de/)).toHaveText(bar!);
	await page.keyboard.press('?');
	await expect(help).toHaveCount(0);
	await page.keyboard.press('Space');
	await expect(page.getByText(/Barra \d+ de/)).not.toHaveText(bar!);
});

test('Bollinger bands overlay shows its range in the readout', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.getByLabel('BB20').check();
	await expect(page.locator('.readout').getByText(/^BB \d+,\d+–\d+,\d+/)).toBeVisible();
});

test('detected candle patterns are named on the chart and open their cheat-sheet card', async ({ page }) => {
	await openTerminal(page, 25);
	const line = page.getByTestId('pattern-line');
	await expect(line.getByRole('button', { name: /Três de fora de alta/ })).toBeVisible();
	await line.getByRole('button', { name: /Três de fora de alta/ }).click();
	const sheet = page.getByRole('dialog', { name: 'Padrões de candle' });
	await expect(sheet.getByTestId('pattern-three-outside-up')).toHaveClass(/focused/);
	await page.keyboard.press('Escape');
	await page.getByLabel('Padrões', { exact: true }).uncheck();
	await expect(line.getByRole('button')).toHaveCount(0);
});

test('regression: hovering bars with and without patterns never moves or resizes the chart', async ({ page }) => {
	// Before: the "Pattern:" row was inserted only while a patterned bar was hovered, pushing the
	// canvas down and shrinking it, so the chart jumped as the mouse crossed bars.
	await openTerminal(page, 25);
	const canvas = page.locator('.chart canvas');
	const box = (await canvas.boundingBox())!;
	const seen = new Set<string>();
	let named = 0;
	for (let x = box.x + 4; x < box.x + box.width * 0.8; x += 6) {
		await page.mouse.move(x, box.y + box.height * 0.3);
		const b = (await canvas.boundingBox())!;
		seen.add(`${b.y.toFixed(1)}:${b.height.toFixed(1)}`);
		named += await page.getByTestId('pattern-line').getByRole('button').count();
	}
	expect(named).toBeGreaterThan(0); // the sweep did cross bars with a detected pattern
	expect([...seen]).toEqual([`${box.y.toFixed(1)}:${box.height.toFixed(1)}`]);
});

test('the top bar has no +5 button; Shift+Space still advances five bars', async ({ page }) => {
	await openTerminal(page, 4242);
	await expect(page.getByRole('button', { name: /\+5/ })).toHaveCount(0);
	const before = await barNumber(page);
	await page.keyboard.press('Shift+Space');
	await expect.poll(() => barNumber(page)).toBe(before + 5);
});

test('chart patterns tab lists multi-bar structures', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.getByRole('button', { name: /Padrões/ }).first().click();
	const sheet = page.getByRole('dialog', { name: 'Padrões de candle' });
	await sheet.getByRole('tab', { name: 'Padrões gráficos' }).click();
	await expect(sheet.getByTestId('pattern-head-and-shoulders')).toBeVisible();
	await sheet.getByRole('radio', { name: 'Alta' }).click();
	await expect(sheet.getByTestId('pattern-double-bottom')).toBeVisible();
	await expect(sheet.getByTestId('pattern-double-top')).toHaveCount(0);
});

async function barNumber(page: Page): Promise<number> {
	const text = (await page.getByText(/Barra \d+ de/).textContent()) ?? '';
	return Number(/Barra (\d+)/.exec(text)?.[1]);
}

test('auto-advance runs on its own and Space takes control back', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.getByRole('combobox', { name: 'Velocidade' }).selectOption({ label: '10×' });
	const start = await barNumber(page);
	await page.getByTestId('auto-toggle').click();
	await expect.poll(() => barNumber(page)).toBeGreaterThanOrEqual(start + 3);
	await page.locator('body').click({ position: { x: 5, y: 5 } }); // focus out of the select
	await page.keyboard.press('Space');
	await expect(page.getByTestId('auto-toggle')).toHaveAttribute('aria-pressed', 'false');
	const stopped = await barNumber(page);
	await page.waitForTimeout(500);
	expect(await barNumber(page)).toBe(stopped);
});

test('auto-advance pauses when an order fills', async ({ page }) => {
	await openTerminal(page, 31);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await page.getByRole('combobox', { name: 'Velocidade' }).selectOption({ label: '10×' });
	await page.getByTestId('auto-toggle').click();
	await expect(page.getByText(/Auto pausado: ordem executada/)).toBeVisible();
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await expect(page.getByTestId('auto-toggle')).toHaveAttribute('aria-pressed', 'false');
});

test('auto-advance waits on a calibration question and resumes after it is answered', async ({ page }) => {
	await openTerminal(page, 4242);
	await page.getByRole('combobox', { name: 'Velocidade' }).selectOption({ label: '10×' });
	await page.getByLabel('pausar em eventos').uncheck();
	await page.getByTestId('auto-toggle').click();
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	await expect(dialog).toBeVisible({ timeout: 15000 });
	const waiting = await barNumber(page);
	await page.waitForTimeout(600);
	expect(await barNumber(page)).toBe(waiting); // never advances behind the question
	await answerIfAsked(page);
	await expect.poll(() => barNumber(page)).toBeGreaterThan(waiting);
	await page.getByTestId('auto-toggle').click();
});

test('soundtrack follows the screen, a closed trade plays a sting, and M mutes (persisted)', async ({ page }) => {
	await page.addInitScript(() => {
		const w = window as unknown as { __played: string[] };
		w.__played = [];
		HTMLMediaElement.prototype.play = function () {
			w.__played.push(this.src.split('/').pop() ?? '');
			return Promise.resolve();
		};
	});
	const played = () => page.evaluate(() => (window as unknown as { __played: string[] }).__played);
	await openTerminal(page, 4242);
	await expect.poll(played).toContain('menu-1.mp3');
	await expect.poll(played).toContain('game-1.mp3');

	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await page.keyboard.press('x');
	await advance(page);
	await expect.poll(async () => (await played()).some((f) => f === 'trade-win.mp3' || f === 'trade-loss.mp3')).toBe(true);

	await page.keyboard.press('m');
	await expect(page.getByTestId('sound-toggle')).toHaveAttribute('aria-pressed', 'false');
	await page.reload();
	await expect(page.getByTestId('sound-on')).not.toBeChecked(); // muted is remembered
	await page.waitForTimeout(300);
	expect(await played()).toEqual([]); // and nothing starts playing
});

test('regression: at 1024px the chart and right rail sit side by side without overlapping', async ({ page }) => {
	// Before: below 1100px the chart and rail were stacked into one fixed-height row and overlapped.
	await page.setViewportSize({ width: 1024, height: 768 });
	await openTerminal(page, 4242);
	const chart = await page.locator('section.chart').boundingBox();
	const rail = await page.getByText('Patrimônio', { exact: false }).first().boundingBox();
	expect(chart && rail).toBeTruthy();
	expect(rail!.x).toBeGreaterThanOrEqual(chart!.x + chart!.width - 1); // rail to the right of the chart
	expect(await page.evaluate(() => document.documentElement.scrollWidth - innerWidth)).toBe(0);
});

test('advisor: a read costs a point, fills the ticket without sending, runs out at 0; the debrief lists it and settles once', async ({ page }) => {
	await openTerminal(page, 15); // m01 seed 15: the first read is a short with a size
	const points = page.getByTestId('advisor-points');
	const ask = page.getByRole('button', { name: /Pedir leitura/ });
	await expect(points).toHaveText('Pontos: 3');
	await ask.click();
	await expect(points).toHaveText('Pontos: 2');
	const advice = page.getByTestId('advice');
	await expect(advice.getByText('SHORT')).toBeVisible();
	await expect(advice.getByText(/confiança 58%/)).toBeVisible();
	await expect(advice.getByText(/Tendência de baixa/)).toBeVisible();
	await expect(advice.getByText(/não uma instrução/)).toBeVisible();
	await expect(ask).toBeDisabled(); // one read per bar

	await page.getByRole('button', { name: /Levar stop e tamanho ao ticket/ }).click();
	await expect(page.getByTestId('ticket-stop').locator('input')).toHaveValue('103.67');
	await expect(page.locator('label:has-text("Quantidade") input')).toHaveValue('41.8');
	await expect(page.getByText(/Nenhuma ordem foi enviada/)).toBeVisible();
	await expect(page.locator('.orders').getByText('STOP')).toHaveCount(0); // nothing was placed

	for (const left of ['Pontos: 1', 'Pontos: 0']) {
		await advance(page);
		await ask.click();
		await expect(points).toHaveText(left);
	}
	await advance(page);
	await expect(ask).toBeDisabled(); // out of points

	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída/).count()); i++) await advance(page, 'Shift+Space');
	const section = page.getByTestId('debrief-advisor');
	await expect(section.locator('tbody tr')).toHaveCount(3);
	await expect(section.locator('tbody tr').first()).toContainText(/certo|errado/);
	await expect(page.getByTestId('debrief-points')).toHaveText('Pontos do assessor: usados 3, ganhos 2. Saldo: 2.');

	// The balance changed once: after a reload the next run starts with it.
	await page.reload();
	await startFromMenu(page, 1);
	await expect(page.getByTestId('advisor-points')).toHaveText('Pontos: 2');
});

test('regression: PT-BR decimal commas in the ticket stop and the plan; an open trade nets its unrealized P&L', async ({ page }) => {
	// Before: "95,56" became NaN (plan coherence 70, timeline "invalidação NaN"), the ticket stop "95,26"
	// zeroed the quantity with a wrong-side warning, and an open trade's Net left out its unrealized P&L.
	await openTerminal(page, 7);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	const stop = page.getByTestId('ticket-stop');
	await stop.getByRole('button', { name: '2×ATR', exact: true }).click();
	const preset = await stop.locator('input').inputValue();
	await stop.locator('input').fill(preset.replace('.', ','));
	await expect(page.getByText(/O stop precisa ficar/)).toHaveCount(0);
	await expect(page.locator('.est dd').first()).toHaveText('10,0');
	await stop.locator('input').fill(''); // hold to the end without a stop: the trade stays open
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	const dialog = page.getByRole('dialog');
	await dialog.locator('textarea').fill('Compro o suporte');
	await dialog.getByLabel('Invalidação').fill('95,56');
	await dialog.getByRole('button', { name: /Registrar plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída/)).toBeVisible();
	await expect(page.getByText(/invalidação 95,56/)).toBeVisible();
	await expect(page.getByText(/NaN/)).toHaveCount(0);
	await expect(page.locator('dt', { hasText: 'Coerência com o plano' }).locator('xpath=following-sibling::dd[1]')).toHaveText('100');
	const row = page.locator('.trades tbody tr').first();
	await expect(row).toContainText('aberto');
	const headline = (await page.locator('.big').first().textContent())!.trim();
	await expect(row.locator('td').nth(4)).toHaveText(headline); // a single open trade: its Net is the result
});

test('regression: letter shortcuts work right after clicking an indicator checkbox', async ({ page }) => {
	// Before: a focused checkbox kept every key but Space, so P and S did nothing until focus moved.
	await openTerminal(page, 8);
	await page.getByLabel('VWAP').check();
	await page.keyboard.press('s');
	await expect(page.getByRole('radio', { name: /Vender \/ Short/ })).toHaveAttribute('aria-checked', 'true');
	await page.keyboard.press('p');
	await expect(page.getByRole('dialog', { name: 'Padrões de candle' })).toBeVisible();
});

test('regression: an open position puts its liquidation price and margin buttons above the rail fold', async ({ page }) => {
	// Before: the position panel sat under the equity panel, below the visible part of the scrolling rail.
	await page.setViewportSize({ width: 1024, height: 768 });
	await openTerminal(page, 303);
	await page.locator('label:has-text("Alavancagem") input').first().fill('5');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	for (const [width, height] of [
		[1024, 768],
		[1440, 900]
	]) {
		await page.setViewportSize({ width, height });
		await page.evaluate(() => scrollTo(0, 0)); // filling the ticket scrolled the page; the rail scrolls on its own
		const rail = (await page.locator('aside.rail').boundingBox())!;
		const add = (await page.getByRole('button', { name: '+ margem' }).boundingBox())!;
		expect(add.y + add.height, `${width}x${height}`).toBeLessThanOrEqual(Math.min(rail.y + rail.height, height));
		expect(await page.locator('aside.rail').evaluate((el) => el.scrollTop)).toBe(0);
	}
});

test('regression: leaving a run for a new one charges its advisor reads', async ({ page }) => {
	// Before: points were settled only at the debrief, so starting over handed every read back.
	await openTerminal(page, 15);
	const points = page.getByTestId('advisor-points');
	await page.getByRole('button', { name: /Pedir leitura/ }).click();
	await expect(points).toHaveText('Pontos: 2');
	await page.getByRole('button', { name: 'Menu', exact: true }).click();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await expect(points).toHaveText('Pontos: 2');
	// Also when the left run is only in storage (after a reload it is offered as "Continue").
	await page.getByRole('button', { name: /Pedir leitura/ }).click();
	await expect(points).toHaveText('Pontos: 1');
	await page.reload();
	await expect(page.getByRole('button', { name: /Continuar partida/ })).toBeVisible();
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(1).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(points).toHaveText('Pontos: 1');
});

test('regression: applying the advisor size raises the leverage to fit the margin; the ticket refuses what it cannot afford', async ({ page }) => {
	// Before: m03 seed 3 applied 120.4 at 1x (margin 12,306 vs 10,000 available): no warning, Send
	// enabled, and the engine answered "insufficient margin".
	await page.goto('/?seed=3');
	await startFromMenu(page, 2);
	await answerIfAsked(page);
	for (let i = 0; i < 7; i++) await advance(page);
	await page.getByRole('button', { name: /Pedir leitura/ }).click();
	await expect(page.getByTestId('advice').getByText('LONG')).toBeVisible();
	await page.getByRole('button', { name: /Levar stop e tamanho ao ticket/ }).click();
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await expect(lev).not.toHaveValue('1');
	await expect(page.getByTestId('ticket-cant-afford')).toHaveCount(0);
	const send = page.getByRole('button', { name: /Enviar ordem/ });
	await expect(send).toBeEnabled();
	await lev.fill('1');
	await expect(page.getByTestId('ticket-cant-afford')).toContainText(/saldo disponível/);
	await expect(send).toBeDisabled();
});

test('regression: the advisor read, its apply button and disclaimer are not clipped in a scroll box', async ({ page }) => {
	// Before: the panel was capped at 45% of the column and scrolled on its own, hiding the apply button.
	await page.setViewportSize({ width: 1440, height: 900 });
	await openTerminal(page, 15);
	await page.getByRole('button', { name: /Pedir leitura/ }).click();
	const panel = page.locator('section.advisor');
	const apply = page.getByRole('button', { name: /Levar stop e tamanho ao ticket/ });
	for (const [width, height] of [
		[1440, 900],
		[1280, 720],
		[1024, 768]
	]) {
		await page.setViewportSize({ width, height });
		await apply.scrollIntoViewIfNeeded();
		await expect(apply).toBeInViewport();
		expect(await panel.evaluate((el) => el.scrollHeight - el.clientHeight)).toBeLessThanOrEqual(1);
		await apply.click();
		await expect(page.getByTestId('advisor-points')).toBeInViewport(); // the header did not scroll away
	}
});

test('regression: after a margin-call bankruptcy the debrief shows the loss the fund covered', async ({ page }) => {
	// Before: the result was -10,000 but the option P&L and trade rows were ~2,500 worse, with no line between.
	await page.goto('/?seed=505');
	await startFromMenu(page, 4);
	await answerIfAsked(page);
	const writePut = async (strike: string, qty: string) => {
		await page.locator('.chain table').first().locator('tbody tr', { has: page.locator('td.strike', { hasText: strike }) }).locator('td').nth(5).getByRole('button').click();
		await page.locator('section.oticket label', { hasText: 'Contratos' }).locator('input').fill(qty);
		await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
		await page.getByRole('button', { name: /Enviar sem plano/ }).click();
		await advance(page);
	};
	await writePut('100,00', '5');
	await writePut('95,00', '880');
	for (let i = 0; i < 60 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page);
	await expect(page.getByRole('heading', { name: 'GAME OVER' })).toBeVisible();
	await expect(page.getByTestId('debrief-written-off')).toHaveText(/^\+[\d.]+,\d\d cr$/);
	// Regression: the market line and buy-and-hold stop at the game-over bar (before, they used prices after it).
	await expect(page.getByTestId('ended-early')).toContainText(/A partida acabou na barra (\d+), antes do fim do cenário .* só até a barra \1\./);
});

test('regression: asking the advisor keeps the leverage typed in the ticket', async ({ page }) => {
	// Before: every new state re-synced the field to the account leverage, so the order went out at 1x.
	await openTerminal(page, 15);
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await lev.fill('3');
	await page.getByRole('button', { name: /Pedir leitura/ }).click();
	await expect(page.getByTestId('advisor-points')).toHaveText('Pontos: 2');
	await expect(lev).toHaveValue('3');
});

test('regression: applying the advisor read keeps the typed leverage and goes back to a market order', async ({ page }) => {
	// Before: apply fitted the size from the account leverage (1x) and set the field to it, and a ticket
	// left on Limite kept its stale price, so the planned risk was not the advisor's ~1%.
	await openTerminal(page, 15);
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	const type = page.locator('label:has-text("Tipo") select').first();
	await lev.fill('3');
	await type.selectOption('limit');
	await page.locator('label:has-text("Preço") input').first().fill('90,00');
	await page.getByRole('button', { name: /Pedir leitura/ }).click();
	await expect(page.getByTestId('advice').getByText('SHORT')).toBeVisible();
	await page.getByRole('button', { name: /Levar stop e tamanho ao ticket/ }).click();
	await expect(page.locator('label:has-text("Quantidade") input')).toHaveValue('41.8');
	await expect(lev).toHaveValue('3');
	await expect(type).toHaveValue('market');
	await expect(page.locator('label:has-text("Preço") input')).toHaveCount(0);
});

test('regression: the planned risk stays in the position panel after a reload and resume', async ({ page }) => {
	await openTerminal(page, 7);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2×ATR', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('dialog').locator('textarea').fill('Compro o suporte');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	const risk = page.locator('aside.rail dt', { hasText: 'Risco planejado' }).locator('xpath=following-sibling::dd[1]');
	const before = await risk.innerText();
	expect(before).toMatch(/^\d+,\d\d cr$/);
	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(risk).toHaveText(before);
});

test('regression: an order against the open position says it reduces, closes or reverses it', async ({ page }) => {
	await openTerminal(page, 7);
	const sizeBy = page.locator('label:has-text("Dimensionar por") select');
	const qty = page.locator('label:has-text("Quantidade") input');
	await sizeBy.selectOption('qty');
	await qty.fill('10');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	const note = page.getByTestId('ticket-opposing');
	await expect(note).toHaveCount(0); // buying more is a new risk, not an opposing order
	await page.keyboard.press('s');
	await qty.fill('4');
	await expect(note).toContainText('ela só reduz a posição para 6,0');
	await qty.fill('10');
	await expect(note).toContainText('fecha a posição inteira');
	await qty.fill('15');
	await expect(note).toContainText('abre 5,0 do outro lado');
	// Sized by risk the quantity ignores the position: the warning says what the order really does.
	await sizeBy.selectOption('risk');
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2×ATR', exact: true }).click();
	await expect(note).toBeVisible();
});

test('regression: the option ticket refuses a market sell when the bid is 0,00', async ({ page }) => {
	await page.goto('/?seed=505');
	await startFromMenu(page, 4);
	await answerIfAsked(page);
	const zeroBid = page.locator('.chain button.q', { hasText: /^0,00$/ }).first();
	for (let i = 0; i < 60 && !(await zeroBid.count()); i++) await advance(page);
	await zeroBid.click();
	await expect(page.getByTestId('opt-no-bid')).toContainText('o bid desta opção é 0,00');
	await expect(page.getByRole('button', { name: /Enviar ordem de opção/ })).toBeDisabled();
});

test('regression: B, S, X and A work after a mouse click on an indicator checkbox', async ({ page }) => {
	await openTerminal(page, 8);
	await page.locator('label.toggle', { hasText: 'EMA50' }).click();
	await page.keyboard.press('s');
	await expect(page.getByRole('radio', { name: /Vender \/ Short/ })).toHaveAttribute('aria-checked', 'true');
	await page.keyboard.press('b');
	await expect(page.getByRole('radio', { name: /Comprar \/ Long/ })).toHaveAttribute('aria-checked', 'true');
	await page.locator('label.toggle', { hasText: 'BB20' }).click();
	await page.keyboard.press('a');
	await expect(page.getByTestId('auto-toggle')).toHaveAttribute('aria-pressed', 'true');
	await page.keyboard.press('a');
	await expect(page.getByTestId('auto-toggle')).toHaveAttribute('aria-pressed', 'false');
});
