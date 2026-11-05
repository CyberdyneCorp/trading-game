import { expect, test, type Page } from '@playwright/test';

// Campaign runs, the curriculum map and the journal screen (add-scenarios-and-gameplay 4.7 and 5.4,
// add-web-client 4.4).

const ALL_MISSIONS = ['m01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer', 'm05-options-writer'];

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

test('campaign: capital carries into the next mission, leverage is gated until Futures II, map and journal follow', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await page.goto('/?seed=4242');
	await page.getByTestId('campaign-start').click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByText(/Campanha — missão 1 de 5/)).toBeVisible();
	await expect(page.getByTestId('campaign-locked')).toContainText('alavancagem acima de 2x');
	await expect(page.getByText('1x – 2x')).toBeVisible(); // the engine reports the gated cap
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.getByTestId('campaign-tag')).toHaveText('CAMPANHA 1/5');
	await answerIfAsked(page);

	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill('Campanha: compro o suporte do range');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
	await playToEnd(page);

	const outcome = page.getByTestId('campaign-outcome');
	await expect(outcome).toContainText('CAMPANHA 1/5');
	await expect(outcome).toContainText('Estágio aprovado: Futuros I');
	const leg = await outcome.getByText(/Capital: /).innerText();
	const carried = leg.split('→')[1].trim();
	await expect(page.getByRole('button', { name: /Jogar de novo/ })).toHaveCount(0); // campaign runs move on instead

	await outcome.getByRole('button', { name: /Seguir para a próxima missão/ }).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByText(/Campanha — missão 2 de 5/)).toBeVisible();
	await expect(page.getByRole('heading', { name: /Missão 03/ })).toBeVisible();
	await expect(briefingCapital(page)).toHaveText(carried);
	await expect(page.getByText('1x – 20x')).toBeVisible(); // Futures II's own mission teaches leverage
	await expect(page.getByTestId('campaign-locked')).toHaveCount(0);

	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByTestId('campaign-progress')).toContainText('Missão 2 de 5');
	await expect(page.getByTestId('campaign-progress')).toContainText(carried);
	await expect(page.getByTestId('mission-card').first().getByText('✓ Concluída')).toBeVisible(); // Completed mark still works

	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=futures1]')).toHaveAttribute('data-status', 'passed');
	await expect(page.locator('[data-stage=futures2]')).toHaveAttribute('data-status', 'open');
	await expect(page.locator('[data-stage=options3]')).toHaveAttribute('data-status', 'locked');
	await expect(page.locator('[data-stage=foundations]')).toHaveAttribute('data-status', 'soon');
	await expect(page.getByTestId('curriculum-summary')).toHaveText('1 de 6 estágios com conteúdo aprovados');
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
	await expect(page.getByTestId('campaign-tag')).toHaveText('CAMPANHA 2/5');
	expect(errors).toEqual([]);
});

test('Bankrupt mid-campaign: game over ends the campaign run; a new one starts from 10,000 with stages intact', async ({ page }) => {
	await page.goto('/?seed=7');
	await seedProgress(page, {
		version: 2,
		tutorialsSeen: ALL_MISSIONS,
		advisorPoints: 3,
		settledRuns: [],
		assessed: ['m01-false-breakout'],
		campaignHistory: [],
		campaign: {
			id: 'camp-e2e',
			startedAt: 1,
			capital: '0.01', // all that is left after a disastrous first mission
			index: 1,
			status: 'active',
			legs: [{ runId: 'r0', missionId: 'm01-false-breakout', startCapital: '10000', endCapital: '0.01', result: 'finished' }]
		}
	});
	await expect(page.getByTestId('campaign-progress')).toContainText('Missão 2 de 5');
	await page.getByTestId('campaign-continue').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await advance(page);
	await expect(page.getByRole('heading', { name: 'GAME OVER' })).toBeVisible();
	const outcome = page.getByTestId('campaign-outcome');
	await expect(outcome).toContainText('A campanha termina aqui');

	await outcome.getByRole('button', { name: /Nova campanha/ }).click();
	await expect(page.getByText(/Campanha — missão 1 de 5/)).toBeVisible();
	await expect(briefingCapital(page)).toHaveText('10.000,00 cr');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByTestId('campaign')).toContainText('Campanhas encerradas: 1');
	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=futures1]')).toHaveAttribute('data-status', 'passed'); // never removed
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
	await expect(progress).toContainText('Missão 1 de 5');
	await expect(progress).not.toContainText('10.000,00 cr'); // the fees and the open trade's P&L stay with the campaign
});

test('a progress record from before campaigns loads: tutorials kept, no campaign yet, nothing passed', async ({ page }) => {
	await page.goto('/?seed=5');
	await seedProgress(page, { tutorialsSeen: ['m01-false-breakout'], advisorPoints: 6, settledRuns: ['old-run'] });
	await expect(page.getByTestId('campaign-start')).toHaveText(/Começar campanha/);
	await page.getByTestId('open-curriculum').click();
	await expect(page.getByTestId('curriculum-summary')).toHaveText('0 de 6 estágios com conteúdo aprovados');
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await expect(page.getByText(/Laboratório sintético/)).toBeVisible(); // tutorial already seen: straight to the briefing
});

test('the journal is empty until a plan is recorded', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-journal').click();
	await expect(page.getByTestId('journal-empty')).toBeVisible();
});
