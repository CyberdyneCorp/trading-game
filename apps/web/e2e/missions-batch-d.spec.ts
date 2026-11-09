import { expect, test, type Page } from '@playwright/test';

// Missions m18-m25 (add-practice-and-advanced-strategies W2-W4): each one played to its debrief. The
// briefing shows the engine's risk limits and margin rules, the tutorial opens on the new drawing, and
// what the seed drew (the reveal, the comparison, the biases) appears only in the debrief.

const INDEX = { m18: 17, m19: 18, m20: 19, m21: 20, m22: 21, m23: 22, m24: 23, m25: 24 } as const;
const REVEAL_DONE = /Missão concluída|GAME OVER/;

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
	for (let i = 0; i < 80 && !(await page.getByText(REVEAL_DONE).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(REVEAL_DONE)).toBeVisible();
}

/** Free play: the tutorial's first drawing, the briefing (title, leverage, limits), then the terminal. */
async function startMission(page: Page, seed: number, index: number, title: RegExp, art: string, leverage = '1x – 3x') {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await expect(page.locator(`svg[data-art=${art}]`)).toBeVisible();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByRole('heading', { name: title })).toBeVisible();
	await expect(page.getByText(leverage)).toBeVisible();
}

async function openTerminal(page: Page) {
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
}

/** A quote in the option chain: `table` = expiry, `row` = strike row (ascending), `col` 2 write call, 3 buy call, 5 write put, 6 buy put. */
function quote(page: Page, table: number, row: number, col: number) {
	return page.locator('.chain table').nth(table).locator('tbody tr').nth(row).locator('td').nth(col).getByRole('button');
}

async function optionLeg(page: Page, table: number, row: number, col: number) {
	await quote(page, table, row, col).click();
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

async function perpWithPlan(page: Page, thesis: string) {
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill(thesis);
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

async function perpWithoutPlan(page: Page) {
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

async function comparisonRows(page: Page, n: number) {
	const cmp = page.getByTestId('debrief-comparison');
	await expect(cmp.getByTestId('comparison-row')).toHaveCount(n);
	await expect(cmp.getByTestId('comparison-net').nth(1)).toHaveText(/^[+-−]?[\d.]+,\d{2} cr$/);
	return cmp;
}

test('m18 hedge: a straddle bought; the briefing lists the delta limit; the debrief compares hedging rules', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMission(page, 1818, INDEX.m18, /Missão 18 · Hedge/, 'scalp');
	await expect(page.getByTestId('brief-risk-limits')).toContainText('Delta em dinheiro ≤ 1.500 bp');
	await expect(page.getByTestId('brief-objective').first()).toContainText(/delta \(em valor\) dentro de 15%/);
	await openTerminal(page);
	await optionLeg(page, 0, 4, 3); // the at-the-money call
	await optionLeg(page, 0, 4, 6); // the at-the-money put
	await playToEnd(page);
	await expect(page.getByText(/GAMMA SCALPING/)).toBeVisible();
	const cmp = await comparisonRows(page, 4);
	await expect(cmp).toContainText(/delta zerado com o perpétuo a cada 4 barras/);
	await expect(page.getByTestId('debrief-regimes')).toBeVisible();
	expect(errors).toEqual([]);
});

test('m19 carry: a synthetic long on the last expiry; 5x; the debrief compares the carry at 5x and 2x', async ({ page }) => {
	await startMission(page, 1919, INDEX.m19, /Missão 19 · Carrego/, 'carry', '1x – 5x');
	await expect(page.getByTestId('brief-risk-limits')).toContainText('Delta em dinheiro ≤ 1.000 bp');
	await openTerminal(page);
	await optionLeg(page, 1, 4, 3); // a call bought on the last expiry
	await optionLeg(page, 1, 4, 5); // the put written at the same strike
	await playToEnd(page);
	await expect(page.getByText(/CARREGO DE FUNDING/)).toBeVisible();
	const cmp = await comparisonRows(page, 4);
	await expect(cmp).toContainText(/perpétuo a 2x/);
});

test('m20 announcement: a straddle; the debrief compares four timings around the release', async ({ page }) => {
	await startMission(page, 2020, INDEX.m20, /Missão 20 · Anúncio/, 'crush');
	await expect(page.getByTestId('brief-risk-limits')).toContainText('Vega por ponto ≤ 150 bp');
	await openTerminal(page);
	await optionLeg(page, 0, 5, 3);
	await optionLeg(page, 0, 5, 6);
	await playToEnd(page);
	await expect(page.getByText(/VOLATILIDADE EM TORNO DE UM EVENTO/)).toBeVisible();
	await comparisonRows(page, 5);
	await expect(page.getByTestId('release-row')).toBeVisible();
});

test('m21 calendar: the next expiry bought, the near one written; calendar margin in the briefing', async ({ page }) => {
	await startMission(page, 2121, INDEX.m21, /Missão 21 · Calendário/, 'calendarSpread');
	await expect(page.getByTestId('brief-margin-rule')).toContainText(/Margem de calendário/);
	await expect(page.getByTestId('brief-risk-limits')).toContainText('Gama de 1% ≤ 50 bp');
	await openTerminal(page);
	await optionLeg(page, 1, 5, 3); // the call of the middle expiry, bought first
	await optionLeg(page, 0, 5, 2); // the near call at the same strike, written
	await playToEnd(page);
	await expect(page.getByText(/CALENDÁRIO: um range/)).toBeVisible();
	await comparisonRows(page, 4);
});

test('m22 management: a planned long; the debrief lists the episode and reads the management objectives', async ({ page }) => {
	await startMission(page, 2222, INDEX.m22, /Missão 22 · Gestão/, 'scale', '1x – 5x');
	await openTerminal(page);
	await perpWithPlan(page, 'Tendência depois da manchete');
	await playToEnd(page);
	await expect(page.getByText(/GESTÃO DA POSIÇÃO/)).toBeVisible();
	await expect(page.getByTestId('debrief-episodes').getByTestId('episode-row')).toHaveCount(1);
	await expect(page.getByTestId('objective-readings').getByTestId('objective-reading')).toHaveCount(3);
	await comparisonRows(page, 4);
});

test('m23 restart: a losing round trip, then an unplanned entry at once; the debrief names the revenge trade with its bars', async ({ page }) => {
	await startMission(page, 2324, INDEX.m23, /Missão 23 · Recomeço/, 'cooldown');
	await expect(page.getByTestId('brief-objective').first()).toContainText(/3 barras seguintes a um trade fechado com perda/);
	await openTerminal(page);
	await perpWithoutPlan(page);
	await advance(page); // filled at the next open
	await page.getByRole('button', { name: /Zerar posição/ }).click();
	await advance(page); // closed at the next open: on this seed the round trip loses
	await perpWithoutPlan(page); // at once, no plan
	await playToEnd(page);
	await expect(page.getByText(/RECOMEÇO/)).toBeVisible();
	const finding = page.locator('.findings li', { hasText: /Vingança: o trade 1 fechou com perda/ });
	await expect(finding).toBeVisible();
	await finding.getByTestId('finding-moment').click();
	const reading = page.getByTestId('objective-reading').first();
	await expect(reading).toHaveAttribute('data-verdict', 'missed');
	await expect(page.getByTestId('debrief-episodes').getByTestId('episode-row')).toHaveCount(2);
	await expect(page.getByTestId('debrief-comparison')).toHaveCount(0);
});

test('m24 rally: waiting is a decision; the debrief reveals the market and reads the planning objective as met', async ({ page }) => {
	await startMission(page, 2424, INDEX.m24, /Missão 24 · Disparada/, 'plan');
	await openTerminal(page);
	await playToEnd(page);
	await expect(page.getByText(/DISPARADA/)).toBeVisible();
	await expect(page.getByTestId('objective-reading').first()).toHaveAttribute('data-verdict', 'met');
});

test('m25 dip: a planned long; the episode and the add objective in the debrief', async ({ page }) => {
	await startMission(page, 2525, INDEX.m25, /Missão 25 · Queda/, 'risk');
	await openTerminal(page);
	await perpWithPlan(page, 'Tendência com recuos');
	await playToEnd(page);
	await expect(page.getByText(/QUEDA: uma tendência/)).toBeVisible();
	await expect(page.getByTestId('debrief-episodes')).toBeVisible();
	await expect(page.getByTestId('objective-reading').first()).toHaveAttribute('data-verdict', 'met');
});
