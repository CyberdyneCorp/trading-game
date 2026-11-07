import { expect, test, type Page } from '@playwright/test';

// Strategy missions m13-m17 (add-academy-and-strategies W10): each one played to its debrief, first with
// the legs placed one by one through the single-leg option ticket, then m13, m14 and m16 again through
// the strategy builder. What the seed drew appears only in the debrief.

const INDEX = { m13: 12, m14: 13, m15: 14, m16: 15, m17: 16 } as const;

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
	for (let i = 0; i < 70 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
}

/**
 * Opens a mission in free play: its tutorial (the new art first), then the briefing with the engine's
 * objectives and the option margin rule (missionRules.spreadMargin; none when writing is off).
 */
async function startMission(page: Page, seed: number, index: number, title: RegExp, art: string) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await expect(page.locator(`svg[data-art=${art}]`)).toBeVisible(); // the first card's drawing
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByRole('heading', { name: title })).toBeVisible();
	await expect(page.getByText('1x – 3x')).toBeVisible(); // the perpetual at 3x, from the engine
	if (art === 'insurance') await expect(page.getByTestId('brief-margin-rule')).toHaveCount(0);
	else await expect(page.getByTestId('brief-margin-rule')).toContainText(/Margem de spread: uma opção vendida coberta/);
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
}

/** Clicks a quote in the option chain: `table` = expiry (0 nearest listed), `row` = strike row (ascending), `col` 2 write call, 3 buy call, 5 write put, 6 buy put. */
function quote(page: Page, table: number, row: number, col: number) {
	return page.locator('.chain table').nth(table).locator('tbody tr').nth(row).locator('td').nth(col).getByRole('button');
}

/** Sends one leg at market; an opening leg asks for a plan first (skipped here), a closing one does not. */
async function optionLeg(page: Page, table: number, row: number, col: number, opens = true) {
	await quote(page, table, row, col).click();
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	if (opens) await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

async function buyPerpWithPlan(page: Page, thesis: string) {
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill(thesis);
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

async function expectObjective(page: Page, text: RegExp) {
	await expect(page.getByText(text).first()).toBeAttached();
}

test('m13 spread: a bull call spread placed leg by leg; the reveal compares it with the naked call', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMission(page, 1313, INDEX.m13, /Missão 13 · Trava/, 'spread');
	await expect(page.getByTestId('debrief-releases')).toHaveCount(0); // nothing of the draw while playing
	await optionLeg(page, 0, 4, 3); // the at-the-money call, bought first
	await optionLeg(page, 0, 5, 2); // the next strike up, written
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/TRAVA contra CALL A SECO/)).toBeVisible();
	await expect(page.getByTestId('debrief-regimes')).toBeVisible();
	await expect(page.getByTestId('release-row')).toContainText(/Adoção institucional · sem aviso/);
	// The engine's strategy comparison, from the player's first option entry, next to the player's result.
	const cmp = page.getByTestId('debrief-comparison');
	await expect(cmp).toContainText(/a da sua primeira entrada em opções/);
	await expect(cmp.getByTestId('comparison-row')).toHaveCount(3);
	await expect(cmp.getByTestId('comparison-row').nth(0)).toContainText(/Você \(esta partida\)/);
	await expect(cmp.getByTestId('comparison-row').nth(1)).toContainText(/Call comprada/);
	await expect(cmp.getByTestId('comparison-row').nth(2)).toContainText(/Trava de alta/);
	await expect(cmp.getByTestId('comparison-net').nth(1)).toHaveText(/^[+-−]?[\d.]+,\d{2} cr$/);
	expect(errors).toEqual([]);
});

test('m14 income: a long perpetual and a written call; the briefing lists the capped-profit objective', async ({ page }) => {
	await page.goto('/?seed=1414');
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(INDEX.m14).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expectObjective(page, /escreva o lucro máximo, que fica limitado pelo strike/);
	await expectObjective(page, /Compare o prêmio recebido com o funding pago pelo long/);
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await buyPerpWithPlan(page, 'Long com venda coberta');
	await optionLeg(page, 0, 8, 2); // a call two strikes up, written
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/RENDA com risco de ALTA FORTE/)).toBeVisible();
	await expect(page.getByTestId('release-row')).toContainText(/Adoção institucional · sem aviso/);
});

test('m15 insurance: buying only; a long with an at-the-money put of the second expiry; the reveal shows the incident', async ({ page }) => {
	await startMission(page, 1515, INDEX.m15, /Missão 15 · Seguro/, 'insurance');
	await buyPerpWithPlan(page, 'Long protegido por put');
	await quote(page, 0, 4, 5).click(); // writing is off in this mission
	await expect(page.getByText('Venda de opções (a descoberto) indisponível nesta missão')).toBeVisible();
	await expect(page.getByRole('button', { name: /Enviar ordem de opção/ })).toBeDisabled();
	await optionLeg(page, 1, 4, 6); // the at-the-money put of the second expiry
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/foi desenhado como um SEGURO/)).toBeVisible();
	await expect(page.getByTestId('release-row')).toContainText(/Incidente em exchange · sem aviso/);
	await expect(page.getByTestId('debrief-comparison').getByTestId('comparison-row')).toHaveCount(5); // you, long, stop, ATM put, OTM put
});

test('m16 premium: an iron condor placed leg by leg, wings first; the reveal shows the breakout', async ({ page }) => {
	await startMission(page, 1616, INDEX.m16, /Missão 16 · Prêmio/, 'condor');
	await optionLeg(page, 0, 1, 6); // long put, four strikes down
	await optionLeg(page, 0, 9, 3); // long call, four strikes up
	await optionLeg(page, 0, 3, 5); // short put, two strikes down
	await optionLeg(page, 0, 7, 2); // short call, two strikes up
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/RANGE seguido de um ROMPIMENTO/)).toBeVisible();
	await expect(page.getByTestId('debrief-regimes')).toBeVisible();
	await expect(page.getByTestId('release-row')).toContainText(/Rumor regulatório · sem aviso/);
});

test('m17 expiry: a written put rolled before its last bars into the next expiry; no headlines to reveal', async ({ page }) => {
	await startMission(page, 1717, INDEX.m17, /Missão 17 · Vencimento/, 'roll');
	await optionLeg(page, 1, 6, 5); // an at-the-money put of the second expiry, written
	for (let i = 0; i < 8; i++) await advance(page, 'Shift+Space'); // 40 bars: 5 before that expiry
	await optionLeg(page, 0, 6, 6, false); // the first listed expiry is now the second: buy the put back
	await optionLeg(page, 1, 6, 5); // and write the next expiry's put at the same strike
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/GESTÃO ANTES DO VENCIMENTO/)).toBeVisible();
	await expect(page.getByTestId('debrief-regimes')).toBeVisible();
	await expect(page.getByTestId('debrief-releases')).toHaveCount(0);
	await expect(page.getByTestId('debrief-comparison')).toContainText(/rolada 5 barras antes/);
});

// The same missions with the strategy builder (the terminal's "Estratégias" tab): the template's legs,
// the engine's analysis, one plan for the combo and the debrief's grouped row.

async function submitTemplate(page: Page, id: string, thesis: string) {
	await page.getByTestId('tab-builder').click();
	await page.getByTestId(`template-${id}`).click();
	await expect(page.getByTestId('builder-preview')).toBeVisible();
	await expect(page.getByTestId('builder-refused')).toHaveCount(0);
	await page.getByTestId('builder-submit').click();
	await page.locator('textarea').first().fill(thesis);
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/pernas? enviadas/)).toBeVisible();
}

test('m13 through the builder: a bull call spread is defined risk, so the debrief credits the process and never calls it naked', async ({ page }) => {
	await startMission(page, 1313, INDEX.m13, /Missão 13 · Trava/, 'spread');
	await page.getByTestId('tab-builder').click();
	await page.getByTestId('template-bull_call').click();
	await expect(page.getByTestId('builder-margin')).toHaveText('0,00 cr'); // a debit spread needs no margin under the spread rule
	await expect(page.getByTestId('builder-max-loss')).not.toHaveText(/ilimitada/);
	await submitTemplate(page, 'bull_call', 'Alta modesta depois da manchete');
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await expect(page.getByTestId('combo-incomplete')).toHaveCount(0);
	await playToEnd(page);

	await expect(page.getByTestId('debrief-combo')).toContainText(/Trava de alta com calls · 2 pernas · completo/);
	await expect(page.getByTestId('debrief-combo-r')).toHaveText(/^[+-]\d+,\d{2}R$/);
	await expect(page.getByText(/vendeu opções a descoberto/)).toHaveCount(0); // scoring v13: the short call is covered
	await expect(page.getByText(/processo bom/).first()).toBeVisible();
});

test('m14 through the builder: the covered call writes against the held long, all of it, as one leg', async ({ page }) => {
	await startMission(page, 1414, INDEX.m14, /Missão 14 · Renda/, 'covered');
	await buyPerpWithPlan(page, 'Long com venda coberta');
	await advance(page);
	await page.getByTestId('tab-builder').click();
	await page.getByTestId('template-covered_call').click();
	await expect(page.getByTestId('builder-use-long')).toBeChecked();
	const label = await page.getByText(/Usar minha posição comprada \(\d+,\d\)/).innerText();
	await expect(page.getByTestId('builder-qty')).toHaveValue(label.match(/\((\d+,\d)\)/)![1]);
	await expect(page.getByTestId('builder-preview').locator('li')).toHaveCount(1); // only the call: the long is already held
	// Regression: the planned risk runs to the long's own stop (2% away), not down to a price of 0.
	await expect(page.getByTestId('builder-size-held')).toContainText(/vai até o stop dela/);
	await page.getByTestId('builder-submit').click();
	const headline = await page.getByTestId('plan-headline').innerText();
	const risk = Number(headline.match(/Risco planejado: ([\d.]+,\d{2})/)![1].replace(/\./g, '').replace(',', '.'));
	expect(risk).toBeLessThan(200); // within 2% of 10.000: no oversized warning for the covered call
	await page.getByRole('button', { name: /^Voltar$/ }).click();
	await submitTemplate(page, 'covered_call', 'Renda sobre o long');
	await advance(page);
	await playToEnd(page);
	await expect(page.getByTestId('debrief-combo')).toContainText(/Venda coberta de call · 1 perna · completo/);
	await expect(page.getByText(/vendeu opções a descoberto/)).toHaveCount(0);
});

test('m16 through the builder: an iron condor fills complete; a short strangle warns that its loss has no ceiling', async ({ page }) => {
	await startMission(page, 1616, INDEX.m16, /Missão 16 · Prêmio/, 'condor');
	await page.getByTestId('tab-builder').click();
	await page.getByTestId('builder-short').check();
	await page.getByTestId('template-strangle').click();
	await expect(page.getByTestId('builder-max-loss')).toHaveText(/ilimitada acima de \d+,\d{2}/);
	await page.getByTestId('builder-submit').click();
	await expect(page.getByTestId('plan-risk-unbounded')).toBeVisible(); // the planned risk shown is only the margin
	await page.getByRole('button', { name: /^Voltar$/ }).click();
	await page.getByTestId('builder-short').uncheck();
	// Sizing aid: contracts from the engine's maximum loss at 1% of equity (not the default 1 contract).
	await page.getByTestId('template-iron_condor').click();
	await expect(page.getByTestId('builder-sizing')).toBeVisible();
	await page.getByTestId('builder-size-risk').fill('1');
	await page.getByTestId('builder-size-apply').click();
	await expect(page.getByTestId('builder-qty')).not.toHaveValue('1');
	// The engine re-analyses the sized legs: their maximum loss is now about 1% of 10.000.
	const loss = async () => {
		const hint = await page.getByTestId('builder-size-hint').innerText();
		return Number(hint.match(/≈ ([\d.]+,\d{2})/)![1].replace(/\./g, '').replace(',', '.'));
	};
	await expect.poll(loss).toBeGreaterThan(80);
	expect(await loss()).toBeLessThanOrEqual(100);
	await submitTemplate(page, 'iron_condor', 'Range até o vencimento');
	await advance(page);
	await expect(page.getByTestId('combo-incomplete')).toHaveCount(0);
	await playToEnd(page);
	await expect(page.getByTestId('debrief-combo')).toContainText(/Iron condor · 4 pernas · completo/);
	await expect(page.getByText(/vendeu opções a descoberto/)).toHaveCount(0);
	await expect(page.getByTestId('release-row')).toContainText(/Rumor regulatório · sem aviso/);
});
