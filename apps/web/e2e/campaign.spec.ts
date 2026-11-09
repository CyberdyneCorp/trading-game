import { expect, test, type Page } from '@playwright/test';

// Campaign runs, the curriculum map and the journal screen (add-scenarios-and-gameplay 4.7 and 5.4,
// add-web-client 4.4).

const ALL_MISSIONS = [
	'm01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer', 'm05-options-writer',
	'm06-thin-book', 'm07-trend-trailing', 'm08-regime-switch', 'm09-event-straddle', 'm10-hedged-book', 'm11-news-calendar',
	'm12-capstone', 'm13-call-spread', 'm14-covered-call', 'm15-protective-put', 'm16-range-condor', 'm17-before-expiry'
];
const M06_LEG = { runId: 'r0', missionId: 'm06-thin-book', startCapital: '10000', result: 'finished' };

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
	for (let i = 0; i < 50 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
}

/** Writes the meta.progress record directly (as an older or later build would have left it), then reloads. */
async function seedProgress(page: Page, progress: Record<string, unknown>) {
	await expect(page.getByTestId('campaign')).toBeVisible(); // the app created its database
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

const briefingCapital = (page: Page) => page.locator('dt:has-text("Capital virtual") + dd');

test('campaign: starts with m06, capital carries into the next mission, leverage is gated until Futures II, map and journal follow', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await page.goto('/?seed=4242');
	await page.getByTestId('campaign-start').click();
	// The campaign's first mission carries the introduction: the chart first, the advisor last.
	await expect(page.getByRole('heading', { name: 'Leia o gráfico' })).toBeVisible();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByText(/Campanha — missão 1 de 21, com o capital inicial da campanha/)).toBeVisible(); // no previous mission
	await expect(page.getByRole('heading', { name: /Missão 06 · Livro fino/ })).toBeVisible();
	await expect(page.getByTestId('campaign-locked')).toContainText('alavancagem acima de 2x');
	await expect(page.getByText('1x – 2x')).toBeVisible(); // m06's own cap, which is also the gated one
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.getByTestId('campaign-tag')).toHaveText('CAMPANHA 1/21');
	await answerIfAsked(page);

	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill('Campanha: compro o suporte do range');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await playToEnd(page);

	const outcome = page.getByTestId('campaign-outcome');
	await expect(outcome).toContainText('CAMPANHA 1/21');
	await expect(outcome).toContainText('Estágios aprovados: Estrutura de mercado, Ordens');
	const leg = await outcome.getByText(/Capital: /).innerText();
	const carried = leg.split('→')[1].trim();
	await expect(page.getByRole('button', { name: /Jogar de novo/ })).toHaveCount(0); // campaign runs move on instead

	await outcome.getByRole('button', { name: /Seguir para a próxima missão/ }).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByText(/Campanha — missão 2 de 21, com o capital da missão anterior/)).toBeVisible();
	await expect(page.getByRole('heading', { name: /Missão 01/ })).toBeVisible();
	await expect(briefingCapital(page)).toHaveText(carried);
	await expect(page.getByText('1x – 2x')).toBeVisible(); // m01 allows 5x, but Futures II is not passed yet
	await expect(page.getByTestId('campaign-locked')).toContainText('alavancagem acima de 2x');

	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByTestId('campaign-progress')).toContainText('Missão 2 de 21');
	await expect(page.getByTestId('campaign-progress')).toContainText(carried);
	await expect(page.getByTestId('mission-card').nth(5).getByText('✓ Concluída')).toBeVisible(); // Completed mark still works

	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=structure]')).toHaveAttribute('data-status', 'passed');
	await expect(page.locator('[data-stage=orders]')).toHaveAttribute('data-status', 'passed');
	await expect(page.locator('[data-stage=futures1]')).toHaveAttribute('data-status', 'open');
	await expect(page.locator('[data-stage=options3]')).toHaveAttribute('data-status', 'locked');
	await expect(page.locator('[data-stage=foundations]')).toHaveAttribute('data-status', 'open'); // taught by its lesson, not passed by the campaign
	await expect(page.getByTestId('curriculum-summary')).toHaveText('2 de 17 estágios com conteúdo aprovados');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();

	await page.getByTestId('open-journal').click();
	const run = page.getByTestId('journal-run');
	await expect(run).toHaveCount(1);
	await expect(run).toContainText('CAMPANHA');
	await expect(run.getByTestId('journal-entry')).toContainText('Campanha: compro o suporte do range');
	await expect(run.getByTestId('journal-entry')).toContainText('confiança');

	// The campaign mission left on its briefing resumes from the menu.
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByTestId('campaign-continue').click();
	await expect(page.getByTestId('campaign-tag')).toHaveText('CAMPANHA 2/21');
	expect(errors).toEqual([]);
});

test('Bankrupt mid-campaign: game over ends the campaign run; a new one starts from 10,000 with stages intact', async ({ page }) => {
	await page.goto('/?seed=7');
	await seedProgress(page, {
		version: 2,
		tutorialsSeen: ALL_MISSIONS,
		coachDone: ['first-trade'], // Space advances: no coach mark holds the focus
		advisorPoints: 3,
		settledRuns: [],
		assessed: ['m06-thin-book'],
		campaignHistory: [],
		campaign: {
			id: 'camp-e2e',
			startedAt: 1,
			capital: '0.01', // all that is left after a disastrous first mission
			index: 1,
			status: 'active',
			legs: [{ ...M06_LEG, endCapital: '0.01' }]
		}
	});
	await expect(page.getByTestId('campaign-progress')).toContainText('Missão 2 de 17'); // a run stored before progress v4 keeps its 17-mission order
	await page.getByTestId('campaign-continue').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await advance(page);
	await expect(page.getByRole('heading', { name: 'GAME OVER' })).toBeVisible();
	const outcome = page.getByTestId('campaign-outcome');
	await expect(outcome).toContainText('A campanha termina aqui');

	await outcome.getByRole('button', { name: /Nova campanha/ }).click();
	await expect(page.getByText(/Campanha — missão 1 de 21/)).toBeVisible();
	await expect(briefingCapital(page)).toHaveText('10.000,00 cr');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByTestId('campaign')).toContainText('Campanhas encerradas: 1');
	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=structure]')).toHaveAttribute('data-status', 'passed'); // never removed
});

test('leaving a campaign mission for free play carries the equity it had; the campaign stays on that mission', async ({ page }) => {
	await page.goto('/?seed=99');
	await page.getByTestId('campaign-start').click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await advance(page);
	await expect(page.getByText(/Execução #1/)).toBeVisible();
	await page.getByRole('button', { name: 'Menu', exact: true }).click();

	await page.getByRole('button', { name: /Iniciar missão/ }).nth(1).click(); // free play replaces the campaign run
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByText(/Laboratório sintético/)).toBeVisible();
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	const progress = page.getByTestId('campaign-progress');
	await expect(progress).toContainText('Missão 1 de 21');
	await expect(progress).not.toContainText('10.000,00 cr'); // the fees and the open trade's P&L stay with the campaign
});

test('a progress record from before campaigns loads: tutorials kept, no campaign yet, nothing passed', async ({ page }) => {
	await page.goto('/?seed=5');
	await seedProgress(page, { tutorialsSeen: ['m01-false-breakout'], advisorPoints: 6, settledRuns: ['old-run'] });
	await expect(page.getByTestId('campaign-start')).toHaveText(/Começar campanha/);
	await page.getByTestId('open-curriculum').click();
	await expect(page.getByTestId('curriculum-summary')).toHaveText('0 de 17 estágios com conteúdo aprovados');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await expect(page.getByText(/Laboratório sintético/)).toBeVisible(); // tutorial already seen: straight to the briefing
});

test('the journal is empty until a plan is recorded', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-journal').click();
	await expect(page.getByTestId('journal-empty')).toBeVisible();
});

test('a liquidation that leaves less than the campaign minimum ends the campaign run: no stage passed, nothing carried', async ({ page }) => {
	await page.goto('/?seed=77');
	await seedProgress(page, {
		version: 2,
		tutorialsSeen: ALL_MISSIONS,
		coachDone: ['first-trade'],
		advisorPoints: 3,
		settledRuns: [],
		assessed: ['m06-thin-book', 'm01-false-breakout'],
		campaignHistory: [],
		campaign: {
			id: 'camp-e2e',
			startedAt: 1,
			capital: '120',
			index: 2,
			status: 'active',
			legs: [
				{ ...M06_LEG, endCapital: '9000' },
				{ runId: 'r1', missionId: 'm01-false-breakout', startCapital: '9000', endCapital: '120', result: 'finished' }
			]
		}
	});
	await page.getByTestId('campaign-continue').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	const lev = page.locator('label:has-text("Alavancagem") input').first();
	await lev.fill('20');
	await lev.dispatchEvent('change');
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.locator('label:has-text("Quantidade") input').first().fill('22');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await playToEnd(page);

	await expect(page.getByText('Missão concluída')).toBeVisible(); // the engine finishes the run (m03 still trades at 20x)
	const outcome = page.getByTestId('campaign-outcome');
	await expect(outcome.getByTestId('campaign-broke')).toContainText('mínimo de 100,00 cr');
	await expect(outcome).toContainText('A campanha termina aqui');
	await expect(outcome).not.toContainText('Estágio aprovado');
	await expect(outcome.getByRole('button', { name: /Seguir para a próxima missão/ })).toHaveCount(0);
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByTestId('campaign-ended')).toContainText('falência na missão 3');
	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=futures2]')).toHaveAttribute('data-status', 'open');
});

test('the campaign summary keeps a space before the best completed capital', async ({ page }) => {
	await page.goto('/?seed=21');
	const done = { id: 'c-old', startedAt: 1, capital: '6357.31', index: 4, status: 'complete', legs: [] };
	await seedProgress(page, {
		version: 2, tutorialsSeen: ALL_MISSIONS, advisorPoints: 3, settledRuns: [], assessed: ALL_MISSIONS, campaignHistory: [done], campaign: done
	});
	await expect(page.getByTestId('campaign')).toContainText('Campanhas encerradas: 1 · Melhor campanha concluída: 6.357,31 cr');
	await page.getByTestId('open-curriculum').click();
	await expect(page.getByText('Campanhas encerradas: 1 · Melhor campanha concluída: 6.357,31 cr')).toBeVisible();
});

test('a run replaced by another one reads as left in the journal, not in progress', async ({ page }) => {
	await page.goto('/?seed=3');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill('Plano que fica para trás');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await advance(page);
	await page.getByRole('button', { name: 'Menu', exact: true }).click();
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(1).click(); // replaces the m01 run
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByTestId('open-journal').click();
	const run = page.getByTestId('journal-run');
	await expect(run).toHaveCount(1);
	await expect(run).toContainText('abandonada');
	await expect(run).not.toContainText('em andamento');
});

test('ending the campaign asks first, then leaves its mission in progress: no campaign run to resume', async ({ page }) => {
	await page.goto('/?seed=42');
	await page.getByTestId('campaign-start').click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await page.getByRole('button', { name: 'Menu', exact: true }).click();
	const resume = page.locator('section.resume');
	await expect(resume).toContainText('CAMPANHA');

	await page.getByTestId('campaign-end').click();
	await expect(page.getByTestId('campaign-end-confirm')).toContainText('Encerrar a campanha?');
	await page.getByRole('button', { name: 'Cancelar' }).click(); // nothing ended
	await expect(page.getByTestId('campaign-progress')).toContainText('Missão 1 de 21');
	await expect(resume).toContainText('CAMPANHA');

	await page.getByTestId('campaign-end').click();
	await page.getByTestId('campaign-end-yes').click();
	await expect(page.getByTestId('campaign-ended')).toContainText('Campanha encerrada');
	await expect(resume).toHaveCount(0); // the old campaign mission is no longer resumable
	await page.reload();
	await expect(page.getByTestId('campaign-ended')).toBeVisible();
	await expect(resume).toHaveCount(0);
});
