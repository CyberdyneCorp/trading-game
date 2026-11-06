import { expect, test, type Page } from '@playwright/test';

// Phase-2 batch B missions (add-scenarios-and-gameplay 6.B): each one played to its debrief. The
// announced calendar (G3) shows kind, bar and consensus while playing; what was drawn (G2) appears
// only in the debrief's per-release table; the capstone closes a campaign run with its report.

const INDEX = { m09: 8, m10: 9, m11: 10, m12: 11 } as const;
const ALL_MISSIONS = [
	'm01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer', 'm05-options-writer',
	'm06-thin-book', 'm07-trend-trailing', 'm08-regime-switch', 'm09-event-straddle', 'm10-hedged-book', 'm11-news-calendar',
	'm12-capstone', 'm13-call-spread', 'm14-covered-call', 'm15-protective-put', 'm16-range-condor', 'm17-before-expiry'
];

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

async function startMission(page: Page, seed: number, index: number, title: RegExp, leverage: string) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByRole('heading', { name: title })).toBeVisible();
	await expect(page.getByText(leverage)).toBeVisible(); // the mission's own cap, from the engine
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
}

async function buyWithPlan(page: Page, thesis: string) {
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill(thesis);
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

/** Clicks a quote in the option chain: `table` = expiry (0 first), `row` = strike row, `col` = 3 buy call, 6 buy put, 2 write call. */
function quote(page: Page, table: number, row: number, col: number) {
	return page.locator('.chain table').nth(table).locator('tbody tr').nth(row).locator('td').nth(col).getByRole('button');
}

async function buyOption(page: Page, table: number, row: number, col: number) {
	await quote(page, table, row, col).click();
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
}

async function expectReleases(page: Page, rows: number) {
	const releases = page.getByTestId('debrief-releases');
	await expect(releases).toBeVisible();
	await expect(releases.getByTestId('release-row')).toHaveCount(rows);
	await expect(releases).toContainText(/[+-]?\d+,\d{2}%/); // the gap, with the PT-BR decimal comma
}

test('m09 the day before: the decision is on the calendar with its consensus; buying only; the debrief reveals the draw', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMission(page, 909, INDEX.m09, /Missão 09 · Véspera/, '1x – 3x');
	const upcoming = page.getByTestId('news-upcoming');
	await expect(upcoming).toContainText(/Decisão de juros na barra \d+ · consenso: neutro/);
	await expect(page.getByTestId('debrief-releases')).toHaveCount(0); // nothing of the draw while playing

	await quote(page, 0, 3, 2).click(); // writing is Options III: locked here
	await expect(page.getByText('Venda de opções (a descoberto) indisponível nesta missão')).toBeVisible();
	await expect(page.getByRole('button', { name: /Enviar ordem de opção/ })).toBeDisabled();
	await buyOption(page, 0, 3, 3); // ATM call
	await buyOption(page, 0, 3, 6); // ATM put: a straddle
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/VÉSPERA DE EVENTO/)).toBeVisible();
	await expectReleases(page, 1);
	await expect(page.getByTestId('release-row')).toContainText(/Decisão de juros · anunciada 20 barras antes/);
	await expect(page.getByTestId('release-implied')).toContainText(/o straddle ATM custava \d+,\d{2}% do preço/);
	await expect(page.getByTestId('release-you')).toHaveText('posicionado antes');
	await expect(page.getByTestId('debrief-greeks')).toBeVisible();
	expect(errors).toEqual([]);
});

test('m10 cover: a long with a protective put; the rumor came unannounced and the reveal says how hard', async ({ page }) => {
	await startMission(page, 1010, INDEX.m10, /Missão 10 · Proteção/, '1x – 5x');
	await expect(page.getByTestId('news-upcoming')).toHaveCount(0); // nothing announced in this mission
	await buyWithPlan(page, 'Compro a alta com proteção');
	await buyOption(page, 1, 2, 6); // a put of the second expiry, one strike below the money
	await advance(page);
	await expect(page.getByText(/OPÇÕES EM CARTEIRA/i)).toBeVisible();
	await playToEnd(page);

	await expect(page.getByText(/LIVRO PROTEGIDO/)).toBeVisible();
	await expectReleases(page, 1);
	await expect(page.getByTestId('release-row')).toContainText(/Rumor regulatório · sem aviso/);
	await expect(page.getByText(/COMPRA P /).first()).toBeVisible();
});

test('m11 calendar: releases are announced with a consensus; the reveal lists each one and where you were', async ({ page }) => {
	await startMission(page, 1111, INDEX.m11, /Missão 11 · Agenda/, '1x – 5x');
	const upcoming = page.getByTestId('news-upcoming');
	for (let i = 0; i < 10 && !(await upcoming.count()); i++) await advance(page, 'Shift+Space');
	await expect(upcoming).toContainText(/Índice de atividade na barra \d+ · consenso: positivo · em \d+ barras?/);
	await buyWithPlan(page, 'Fico pequeno antes da divulgação');
	await playToEnd(page);

	await expect(page.getByText(/AGENDA: um índice de atividade/)).toBeVisible();
	await expectReleases(page, 4);
	await expect(page.getByTestId('debrief-releases')).toContainText('Incidente em exchange · sem aviso');
	await expect(page.getByTestId('release-you').first()).toHaveText(/posicionado antes|entrou depois|antes e depois|fora/);
	await expect(page.getByTestId('debrief-regimes')).toBeVisible();
});

test('m12 final exam in free play: the reveal, no campaign report outside a campaign run', async ({ page }) => {
	await startMission(page, 1212, INDEX.m12, /Missão 12 · Prova final/, '1x – 10x');
	await buyWithPlan(page, 'Lateral: compro o suporte com stop');
	await playToEnd(page);

	await expect(page.getByText(/PROVA FINAL/)).toBeVisible();
	await expect(page.getByTestId('debrief-regimes')).toBeVisible();
	await expectReleases(page, 3);
	await expect(page.getByTestId('campaign-report')).toHaveCount(0);
});

/** Writes the meta.progress record directly, then reloads (as campaign.spec.ts does). */
async function seedProgress(page: Page, progress: Record<string, unknown>) {
	await expect(page.getByTestId('campaign')).toBeVisible();
	await page.evaluate(
		(p) =>
			new Promise<void>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => {
					const tx = open.result.transaction('meta', 'readwrite');
					tx.objectStore('meta').put({ key: 'progress', ...p });
					tx.oncomplete = () => (open.result.close(), resolve());
					tx.onerror = () => reject(tx.error);
				};
				open.onerror = () => reject(open.error);
			}),
		progress
	);
	await page.reload();
	await expect(page.getByTestId('campaign')).toBeVisible();
}

test('the capstone closes the campaign run with its report: too few trades to conclude', async ({ page }) => {
	await page.goto('/?seed=1213');
	// Sixteen settled missions, one trade each (a win, then a loss, alternately: fewer than 30 trades in
	// all), with the engine facts each debrief stored.
	const before = ALL_MISSIONS.filter((m) => m !== 'm12-capstone');
	const order = ['m06-thin-book', 'm01-false-breakout', 'm03-volatility-shock', 'm02-funding-squeeze', 'm07-trend-trailing',
		'm04-options-buyer', 'm09-event-straddle', 'm05-options-writer', 'm10-hedged-book', 'm13-call-spread', 'm14-covered-call',
		'm15-protective-put', 'm16-range-condor', 'm17-before-expiry', 'm08-regime-switch', 'm11-news-calendar'];
	const win = { net: '120', rBp: 15000, regime: 'trend' };
	const loss = { net: '-60', rBp: -10000, regime: 'range' };
	const legs = order.map((missionId, i) => ({
		runId: `r${i}`, missionId, startCapital: String(10000 + 50 * i), endCapital: String(10050 + 50 * i), result: 'finished',
		report: { trades: [i % 2 === 0 ? win : loss], netPnl: '50', fees: '6', spreadSlippage: '4', funding: '-0.5' }
	}));
	await seedProgress(page, {
		version: 2, tutorialsSeen: ALL_MISSIONS, advisorPoints: 3, settledRuns: [], assessed: before, campaignHistory: [],
		campaign: { id: 'camp-final', startedAt: 1, capital: '10800', index: 16, status: 'active', legs }
	});
	await expect(page.getByTestId('campaign-progress')).toContainText('Missão 17 de 17');
	await page.getByTestId('campaign-continue').click();
	await expect(page.getByRole('heading', { name: /Missão 12 · Prova final/ })).toBeVisible();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await buyWithPlan(page, 'Prova final: compro com stop e 1% de risco');
	await playToEnd(page);

	const r = page.getByTestId('campaign-report');
	await expect(r).toBeVisible();
	await expect(r).toContainText('17 missões desta campanha'); // the sixteen seeded legs and this run
	await expect(r.getByTestId('report-sample')).toContainText(/Só \d+ trades: poucos demais para concluir/);
	await expect(r.getByTestId('report-expectancy')).toHaveText(/^[+-]\d+,\d{2}R$/);
	await expect(r.getByTestId('report-cost-share')).toHaveText(/\d+,\d%|P&L bruto negativo/);
	await expect(r).toContainText('Por regime revelado');
	await expect(page.getByTestId('campaign-outcome')).toContainText('CAMPANHA 17/17');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=evaluation]')).toHaveAttribute('data-status', 'passed');
	await expect(page.locator('[data-stage=events]')).toHaveAttribute('data-status', 'passed');
});
