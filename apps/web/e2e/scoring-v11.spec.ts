import { expect, test, type Page } from '@playwright/test';

// Scoring v11 in the game (add-scenarios-and-gameplay 4.1, 4.2, 4.5, 4.8): the briefing renders the
// engine's mission rules, lesson questions are engine commands corrected in the debrief, every
// decision is recorded, and rewinding makes an assisted attempt that never counts for progress.

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
	for (let i = 0; i < 60 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) await advance(page, 'Shift+Space');
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
}

async function openBriefing(page: Page, seed: number, index: number) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click(); // every built-in mission has a tutorial
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();
}

test('briefing: objectives, permissions and limits come from the engine mission rules', async ({ page }) => {
	await openBriefing(page, 21, 1); // m02
	const objectives = page.getByTestId('brief-objective');
	await expect(objectives).toHaveCount(3);
	await expect(objectives.nth(2)).toHaveText('Mantenha a distância até a liquidação acima de 10%.');
	await expect(page.getByTestId('brief-permissions')).toHaveText('long · short');
	await expect(page.getByTestId('brief-not-allowed')).toHaveText('comprar opções · vender opções');
	await expect(page.getByTestId('brief-limits')).toHaveText('Aviso de liquidação a 10% do preço · orçamento de drawdown 20% do capital');
	await openBriefing(page, 21, 8); // m09: options buying only
	await expect(page.getByTestId('brief-permissions')).toHaveText('long · short · comprar opções');
	await expect(page.getByTestId('brief-not-allowed')).toHaveText('vender opções');
	await expect(page.getByTestId('brief-objective').nth(2)).toHaveText('Não pague mais que 2% do patrimônio em prêmio.');
});

test('lesson questions are answered once on the briefing and corrected in the debrief; decisions are listed', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await openBriefing(page, 77, 0); // m01
	const quiz = page.getByTestId('lesson-quiz');
	await expect(quiz).toBeVisible();
	await expect(page.getByTestId('lesson-unreviewed')).toBeVisible();
	const questions = quiz.getByTestId('lesson-question');
	await questions.nth(0).getByRole('radio', { name: /ponto de invalidação/ }).check();
	await questions.nth(0).getByRole('button', { name: 'Responder' }).click();
	await expect(questions.nth(0).getByTestId('lesson-done')).toBeVisible();
	await questions.nth(1).locator('input').fill('abc');
	await questions.nth(1).getByRole('button', { name: 'Responder' }).click();
	await expect(questions.nth(1).getByText(/Digite um número/)).toBeVisible();
	await questions.nth(1).locator('input').fill('199,5'); // decimal comma accepted; 2% of 10.000 is 200
	await questions.nth(1).getByRole('button', { name: 'Responder' }).click();
	await expect(questions.nth(1).getByTestId('lesson-done')).toBeVisible();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await advance(page); // wait
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page); // act
	await advance(page); // hold
	await playToEnd(page);
	const lessons = page.getByTestId('debrief-lessons');
	await expect(lessons).toContainText('1 de 2 pontos');
	await expect(lessons).toContainText(/não contam na nota de compreensão/);
	await expect(lessons.getByTestId('lesson-result').nth(1)).toContainText('Sua resposta: 199,5');
	await expect(lessons.getByTestId('lesson-result').nth(1)).toContainText('Resposta certa: 200');
	const decisions = page.getByTestId('debrief-decisions');
	await expect(decisions.getByTestId('decisions-summary')).toContainText(/Agiu em 1, manteve a posição em \d+ e esperou fora do mercado em \d+ de \d+ decisões/);
	await expect(decisions.locator('[data-kind="wait"]').first()).toBeVisible();
	expect(errors).toEqual([]);
});

test('assisted practice: a rewind is a new attempt flagged assisted; the original stays and progress does not count it', async ({ page }) => {
	await openBriefing(page, 88, 0); // m01
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	for (let i = 0; i < 6; i++) await advance(page);
	await expect(page.getByText(/Barra 7 de/)).toBeVisible();
	const assist = page.getByTestId('assist-panel');
	await assist.locator('summary').click();
	await assist.getByTestId('assist-turn').fill('3');
	await assist.getByTestId('assist-rewind').click();
	await expect(page.getByTestId('assisted-tag')).toHaveText('ASSISTIDA');
	await expect(page.getByText(/Barra 3 de/)).toBeVisible();
	await playToEnd(page);
	await expect(page.getByTestId('debrief-assisted')).toContainText(/não conta para o progresso/);
	await expect(page.getByTestId('debrief-points')).toContainText(/usados 0, ganhos 0/);
	await page.getByRole('button', { name: 'Voltar às missões' }).click();
	// Neither the assisted attempt (finished) nor the original (left) completes the mission.
	await expect(page.getByText('Concluída', { exact: true })).toHaveCount(0);
	await page.reload();
	await expect(page.getByText('Concluída', { exact: true })).toHaveCount(0);
});
