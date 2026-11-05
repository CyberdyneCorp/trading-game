import { expect, test, type Page } from '@playwright/test';

// Intel perks (add-advisor-intel-tutorials phase 3): unlocks from distinct finished missions,
// briefing toggles and cards, early headlines, the economic calendar and the "with intel" label.

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

/** Stores finished runs of these missions (as earlier sessions would have), then reloads the menu. */
async function seedFinished(page: Page, missions: string[]) {
	await expect(page.getByTestId('campaign')).toBeVisible(); // the app created its database
	await page.evaluate(
		(ids) =>
			new Promise<void>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => {
					const tx = open.result.transaction('runs', 'readwrite');
					ids.forEach((missionId, i) =>
						tx.objectStore('runs').put({
							runId: `seed-${i}`, missionId, seed: i, createdAt: i, executionPolicy: 12, journal: [], questions: [],
							stopWarnings: 0, digest: '', status: 'finished', actions: 0
						})
					);
					tx.oncomplete = () => (open.result.close(), resolve());
					tx.onerror = () => reject(tx.error);
				};
				open.onerror = () => reject(open.error);
			}),
		missions
	);
	await page.reload();
	await expect(page.getByTestId('campaign')).toBeVisible();
}

/** Free play of the n-th mission in the menu, up to its briefing (skipping its first-start tutorial). */
async function briefing(page: Page, index: number, firstStart = true) {
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	if (firstStart) await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();
}

async function enterTerminal(page: Page) {
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	await answerIfAsked(page);
}

test('perks: locked with their requirement until missions are finished; each is explained once', async ({ page }) => {
	await page.goto('/?seed=11');
	await briefing(page, 0);
	await expect(page.getByTestId('perk-locked-calendar')).toContainText('conclua 1 missão (0 de 1)');
	await expect(page.getByTestId('perk-locked-wire1')).toContainText('Feed expresso I');
	await expect(page.getByTestId('perk-locked-wire2')).toContainText('conclua 4 missões diferentes (0 de 4)');
	await expect(page.getByRole('checkbox')).toHaveCount(0);
	await page.getByRole('button', { name: /Voltar às missões/ }).click();

	await seedFinished(page, ['m03-volatility-shock', 'm03-volatility-shock']); // a repeat counts once
	await briefing(page, 0, false);
	await expect(page.getByTestId('perk-intro-calendar')).toContainText('Calendário econômico desbloqueado');
	await expect(page.getByTestId('perk-intro-calendar')).toContainText('legal');
	await expect(page.getByTestId('perk-calendar')).toBeChecked(); // on by default
	await expect(page.getByTestId('perk-locked-wire1')).toContainText('conclua 2 missões diferentes (1 de 2)');
	await enterTerminal(page);

	await page.getByRole('button', { name: 'Menu', exact: true }).click();
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await expect(page.getByTestId('perk-calendar')).toBeChecked();
	await expect(page.getByTestId('perk-intro-calendar')).toHaveCount(0); // explained once
});

test('Fast wire I: a second finished mission explains it, headlines show one bar early, the run is labeled with intel', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await page.goto('/?seed=11');
	await seedFinished(page, ['m02-funding-squeeze', 'm03-volatility-shock']);
	await briefing(page, 0);
	await expect(page.getByTestId('perk-intro-wire1')).toContainText('Feed expresso I desbloqueado');
	await expect(page.getByTestId('perk-intro-wire1')).toContainText('não informação privilegiada');
	await expect(page.getByTestId('perk-wire1')).toBeChecked();
	await enterTerminal(page);

	// A plan and an entry, so the run appears in the journal.
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.locator('textarea').fill('Range com feed expresso');
	await page.getByRole('button', { name: /Registrar plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();

	const early = page.getByTestId('news-early');
	for (let i = 0; i < 60 && !(await early.count()); i++) await advance(page);
	await expect(early.first()).toContainText('ANTECIPADA');
	await expect(early.first()).toContainText('pública em 1 barra');
	await advance(page);
	await expect(early).toHaveCount(0); // public now: no longer flagged

	for (let i = 0; i < 50 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByTestId('debrief-intel')).toContainText('COM INTEL');
	await expect(page.getByTestId('debrief-intel')).toContainText('Calendário econômico, Feed expresso I');

	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByTestId('open-journal').click();
	await expect(page.getByTestId('journal-run').first().getByTestId('journal-intel')).toHaveText('COM INTEL');
	expect(errors).toEqual([]);
});

test('economic calendar: m02 shows the rate decision bar only; a perk toggled off is not used', async ({ page }) => {
	await page.goto('/?seed=2026');
	await seedFinished(page, ['m01-false-breakout', 'm03-volatility-shock']);
	await briefing(page, 1);
	await page.getByTestId('perk-wire1').uncheck();
	await expect(page.getByTestId('perk-wire1')).not.toBeChecked();
	await enterTerminal(page);

	const calendar = page.getByTestId('news-calendar');
	await expect(calendar).toContainText(/Agenda: decisão de juros na barra \d+/);
	await expect(page.locator('.news ol').getByText(/Banco Central/)).toHaveCount(0); // nothing about its content
	for (let i = 0; i < 80 && (await calendar.count()); i++) await advance(page, 'Shift+Space');
	await expect(calendar).toHaveCount(0);
	await expect(page.locator('.news ol').getByText(/Banco Central de Varena/)).toBeVisible();
	await expect(page.getByTestId('news-early')).toHaveCount(0); // the wire was off
});

test('campaign missions start without perks so campaign runs stay comparable', async ({ page }) => {
	await page.goto('/?seed=4242');
	await seedFinished(page, ['m01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer']);
	await page.getByTestId('campaign-start').click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByTestId('perks-campaign-off')).toBeVisible();
	await expect(page.getByTestId('perks')).toHaveCount(0);
	await expect(page.getByRole('checkbox')).toHaveCount(0);
});

test('regression: a perk checkbox sits on the same line as its name', async ({ page }) => {
	// Before: the global "input { width: 100% }" stretched it, so it was centered alone above the label.
	await page.goto('/?seed=11');
	await seedFinished(page, ['m03-volatility-shock']);
	for (const width of [1024, 1440]) {
		await page.setViewportSize({ width, height: 900 });
		await briefing(page, 0, width === 1024);
		const box = (await page.getByTestId('perk-calendar').boundingBox())!;
		const name = (await page.getByTestId('perks').locator('li strong').first().boundingBox())!;
		expect(box.width).toBeLessThan(40);
		expect(name.x).toBeGreaterThan(box.x + box.width - 1); // to its right
		expect(Math.abs(name.y + name.height / 2 - (box.y + box.height / 2))).toBeLessThan(name.height); // same line
		await page.getByRole('button', { name: /Voltar às missões/ }).click();
	}
});
