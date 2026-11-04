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
	await expect(feed.getByText(/\(STOP sell/)).toBeVisible();
	await expect(feed.getByText(/\(TP sell/)).toBeVisible();
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

test('options mission: buy a call from the chain with a plan, hold to expiry, see settlement', async ({ page }) => {
	await page.goto('/?seed=12');
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(3).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
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
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(3).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.locator('.chain table').first().locator('tbody tr').nth(2).locator('td').nth(6).getByRole('button').click();
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	for (let i = 0; i < 40 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída/)).toBeVisible();
	await expect(page.getByText(/COMPRA P 100/)).toBeVisible();
	await expect(page.locator('.trades').getByText('vencimento')).toBeVisible();
	await expect(page.getByText(/Você não operou/)).toHaveCount(0);
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
	await expect(stop.getByText(/2\.0×ATR/)).toBeVisible();
	await tp.getByRole('button', { name: '2R', exact: true }).click();
	expect(Number(await tp.locator('input').inputValue())).toBeGreaterThan(stopValue);
	await expect(tp.getByText(/R:R 1:2\.0/)).toBeVisible();
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
