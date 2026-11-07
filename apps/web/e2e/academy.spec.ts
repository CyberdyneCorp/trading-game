import AxeBuilder from '@axe-core/playwright';
import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { answerIfAsked, playToEnd } from './helpers.ts';

// Academy, glossary and guided first trade (add-academy-and-strategies tasks W2–W4, W8, W9): every
// lesson to its end from the Academy (a wrong-then-right first answer in each), the spaced-review
// prompt with a mocked clock, the glossary screen and in-place definitions, and the coach-mark tour
// (shown once, skip, replay, keyboard only), with axe on each new screen.

interface Question {
	type?: 'choice' | 'number' | 'set';
	answer?: number;
	choices?: unknown[];
	field?: string;
	target?: number;
}
interface Step {
	id: string;
	question?: Question;
	widget?: { kind: string; entry?: number; stop?: number | null };
}
const read = (name: string) => JSON.parse(readFileSync(new URL(`../src/lib/content/academy/${name}.json`, import.meta.url), 'utf8'));
const INDEX: { id: string; title: { 'pt-BR': string } }[] = read('index').lessons;
const steps = (id: string): Step[] => read(id).steps;

async function expectNoViolations(page: Page, screen: string) {
	const { violations } = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa']).analyze();
	expect(violations.map((v) => `${screen}: ${v.id} (${v.impact}) ${v.nodes.length}× ${v.nodes[0]?.target.join(' ')}`)).toEqual([]);
}

const ptNumber = (v: number) => String(v).replace('.', ',');

async function answerWrong(page: Page, q: Question) {
	if (q.type === 'number') {
		await page.getByTestId('lesson-number').fill(ptNumber((q.answer ?? 0) + 1000));
		await page.getByTestId('lesson-check').click();
	} else if (q.type === 'set') {
		await page.getByTestId('lesson-check').click(); // the sandbox starts away from the target
	} else {
		await page.getByTestId('lesson-choice').nth(((q.answer ?? 0) + 1) % (q.choices?.length ?? 2)).click();
	}
	await expect(page.getByTestId('lesson-feedback')).toContainText('Tente de novo.');
	await expect(page.getByTestId('lesson-next')).toBeDisabled();
}

async function answerRight(page: Page, step: Step) {
	const q = step.question!;
	if (q.type === 'number') {
		await page.getByTestId('lesson-number').fill(ptNumber(q.answer!));
		await page.getByTestId('lesson-check').click();
	} else if (q.type === 'set') {
		if (q.field === 'leverage') await page.getByTestId('sandbox-leverage').fill(String(q.target));
		else await page.getByTestId('sandbox-qty').fill(ptNumber(q.target! / Math.abs(step.widget!.entry! - step.widget!.stop!)));
		await page.getByTestId('lesson-check').click();
	} else {
		await page.getByTestId('lesson-choice').nth(q.answer!).click();
	}
	await expect(page.getByTestId('lesson-feedback')).toContainText('Correto.');
}

/** Runs a lesson from its first step to the end screen; the first question is answered wrong first. */
async function runLesson(page: Page, id: string) {
	let wrongDone = false;
	for (const s of steps(id)) {
		if (s.question) {
			if (!wrongDone) await answerWrong(page, s.question);
			wrongDone = true;
			await answerRight(page, s);
		}
		await page.getByTestId('lesson-next').click();
	}
	await expect(page.getByTestId('lesson-done')).toBeVisible();
}

async function seedProgress(page: Page, progress: Record<string, unknown>) {
	await page.evaluate(async (p) => {
		const db = await new Promise<IDBDatabase>((resolve, reject) => {
			const open = indexedDB.open('tradelab');
			open.onsuccess = () => resolve(open.result);
			open.onerror = () => reject(open.error);
		});
		await new Promise<void>((resolve, reject) => {
			const tx = db.transaction('meta', 'readwrite');
			tx.objectStore('meta').put({ key: 'progress', ...p });
			tx.oncomplete = () => resolve();
			tx.onerror = () => reject(tx.error);
		});
		db.close();
	}, progress);
	await page.reload();
}

test('every Academy lesson runs to its end with a wrong-then-right answer, and the Academy marks it done', async ({ page }) => {
	test.setTimeout(240_000);
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await page.goto('/');
	await page.getByTestId('open-academy').click();
	await expect(page.getByTestId('academy-lesson')).toHaveCount(INDEX.length);
	await expectNoViolations(page, 'academy');
	for (const { id } of INDEX) {
		const row = page.locator(`[data-lesson="${id}"]`);
		await expect(row).toHaveAttribute('data-status', 'new');
		await row.getByTestId('academy-start').click();
		await runLesson(page, id);
		await expect(page.getByTestId('lesson-score')).toContainText(/respostas certas na primeira tentativa/);
		await page.getByTestId('lesson-finish').click();
		await expect(page.locator(`[data-lesson="${id}"]`)).toHaveAttribute('data-status', 'done');
	}
	// Lessons never pass a stage that has missions: only the lesson-only stages are passed.
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=foundations]')).toHaveAttribute('data-status', 'passed');
	await expect(page.locator('[data-stage=futures2]')).toHaveAttribute('data-status', 'locked');
	await expect(page.locator('[data-stage=risk] [data-testid=stage-lessons]')).toHaveText('Lições da Academia: 2 de 2 concluídas');
	expect(errors).toEqual([]);
});

test('a set question with the ticket sandbox, a payoff and a table pass axe; a recommended lesson returns to its map', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-curriculum').click();
	await page.locator('[data-stage=risk] [data-lesson=risk-sizing]').getByTestId('lesson-start').click();
	for (let i = 0; i < 2; i++) await page.getByTestId('lesson-next').click();
	await expect(page.getByTestId('lesson-ticket')).toBeVisible();
	await expectNoViolations(page, 'lesson with the ticket sandbox');
	// Sizing by moving the ticket: 25 units at entry 100 and stop 96 risk 1% of 10,000.
	await page.getByTestId('sandbox-qty').fill('24');
	await page.getByTestId('lesson-check').click();
	await expect(page.getByTestId('lesson-feedback')).toContainText('Correto.'); // within one lot
	await expect(page.getByTestId('sandbox-risk')).toContainText('96,00 cr');
	await page.getByTestId('lesson-next').click();
	await page.getByTestId('lesson-number').fill('2');
	await page.getByTestId('lesson-check').click();
	await page.getByRole('button', { name: 'Sair da lição' }).click();
	await expect(page.getByTestId('curriculum-summary')).toBeVisible(); // back to the map it came from

	await page.locator('[data-lesson=options-payoff]').getByTestId('lesson-start').click();
	for (let i = 0; i < 2; i++) await page.getByTestId('lesson-next').click();
	await expect(page.getByTestId('payoff-analytics')).toContainText('104,00');
	await expectNoViolations(page, 'lesson with a payoff');
	await page.getByRole('button', { name: 'Sair da lição' }).click();
	await page.locator('[data-lesson=risk-expectancy]').getByTestId('lesson-start').click();
	await page.getByTestId('lesson-next').click();
	await expect(page.getByTestId('lesson-table')).toBeVisible();
	await expectNoViolations(page, 'lesson with a table');
});

test('Review due: the menu prompts a day later, the review asks only decision and transfer questions, a miss resets it', async ({ page }) => {
	await page.clock.setFixedTime(new Date(2026, 2, 10, 12));
	await page.goto('/');
	await page.getByTestId('open-academy').click();
	await page.locator('[data-lesson=futures-pnl]').getByTestId('academy-start').click();
	await runLesson(page, 'futures-pnl');
	await page.getByTestId('lesson-finish').click();
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await expect(page.getByTestId('review-prompt')).toHaveCount(0); // not due on the same day

	await page.clock.setFixedTime(new Date(2026, 2, 11, 9));
	await page.reload();
	await expect(page.getByTestId('review-prompt')).toHaveText(/1 lição para revisar hoje/);
	await page.getByTestId('review-dismiss').click();
	await expect(page.getByTestId('review-prompt')).toHaveCount(0);
	await page.reload();
	await expect(page.getByRole('button', { name: /Iniciar missão/ }).first()).toBeVisible();
	await expect(page.getByTestId('review-prompt')).toHaveCount(0); // dismissed until tomorrow
	await page.getByTestId('open-academy').click();
	await expect(page.locator('[data-lesson=futures-pnl]')).toHaveAttribute('data-status', 'due'); // still due in the Academy
	await page.getByTestId('academy-due').getByTestId('review-start').click();
	await expect(page.getByTestId('lesson-step')).toHaveText('1 de 3'); // short, fees, level
	await page.getByTestId('lesson-number').fill('-18');
	await page.getByTestId('lesson-check').click(); // Review failed: wrong on the first try
	await page.getByTestId('lesson-number').fill('18');
	await page.getByTestId('lesson-check').click();
	await page.getByTestId('lesson-next').click();
	await page.getByTestId('lesson-choice').first().click();
	await page.getByTestId('lesson-next').click();
	await page.getByTestId('lesson-number').fill('-30');
	await page.getByTestId('lesson-check').click();
	await page.getByTestId('lesson-next').click();
	await expect(page.getByTestId('review-outcome')).toContainText('volta a ser revisada amanhã');
	await page.getByTestId('lesson-finish').click();
	await expect(page.locator('[data-lesson=futures-pnl]')).toHaveAttribute('data-status', 'done');

	await page.clock.setFixedTime(new Date(2026, 2, 12, 9));
	await page.reload();
	await expect(page.getByTestId('review-prompt')).toHaveText(/1 lição para revisar hoje/); // a new day: shown again
});

test('glossary: search without accents, related terms, axe; Definition on focus in the ticket opens the glossary', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByTestId('open-glossary').click();
	await page.getByTestId('glossary-search').fill('liquidacao');
	const hit = page.getByTestId('glossary-term').filter({ hasText: 'Preço de liquidação' });
	await expect(hit).toBeVisible();
	await hit.click();
	await expect(page.getByTestId('glossary-entry')).toContainText('Futuros II');
	await expect(page.getByTestId('glossary-entry')).toContainText('Relacionados');
	await expectNoViolations(page, 'glossary');
	await page.getByTestId('glossary-back').click();

	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	const next = page.getByRole('button', { name: /Pular tutorial|Abrir terminal/ }).first();
	if ((await next.innerText()).includes('Pular tutorial')) await next.click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	const liq = page.locator('.ticket [data-term="liquidation-price"]');
	await liq.focus();
	await expect(page.locator('.ticket [data-testid=term-tip]')).toContainText('O preço em que a margem da posição chega à manutenção');
	await expect(liq).toHaveAccessibleDescription(/O preço em que a margem da posição chega à manutenção/);
	const turn = await page.locator('.turn strong').innerText();
	await page.keyboard.press('Enter');
	const dialog = page.getByTestId('glossary-dialog');
	await expect(dialog.getByTestId('glossary-entry')).toContainText('Preço de liquidação');
	await page.keyboard.press('Space'); // the dialog is modal: the market does not advance behind it
	await page.keyboard.press('Escape');
	await expect(dialog).toHaveCount(0);
	await expect(page.locator('.turn strong')).toHaveText(turn);
});

test('guided first trade: shown on the first campaign terminal, keyboard only, skip, replay from help, once per device', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByTestId('campaign-start').click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	const mark = page.getByTestId('coach-mark');
	await expect(mark).toHaveAttribute('data-step', 'chart');
	await expect(page.getByTestId('coach-next')).toBeFocused();
	await expect(page.locator('[data-coach=chart]')).toHaveAccessibleDescription(/Cada barra fechada é informação/);
	await expect(page.getByTestId('coach-live')).toHaveText(/Passo 1 de 6/);
	await expectNoViolations(page, 'coach mark');

	// Space on a coach mark: the tour moves on, the market does not.
	const turn = await page.locator('.turn strong').innerText();
	await page.keyboard.press('Space');
	await expect(mark).toHaveAttribute('data-step', 'ticket');
	await expect(page.locator('.turn strong')).toHaveText(turn);
	await page.keyboard.press('Enter');
	await expect(mark).toHaveAttribute('data-step', 'stop');
	await page.getByTestId('ticket-stop').locator('input').fill('90'); // a typed stop completes the step
	await expect(mark).toHaveAttribute('data-step', 'risk');
	await page.getByTestId('coach-next').click();
	await expect(mark).toHaveAttribute('data-step', 'advance');
	await page.keyboard.press('Escape'); // Skip and Esc end the tour and mark it done
	await expect(mark).toHaveCount(0);

	// Replay from help: starts at the chart step, in this run.
	await page.getByRole('button', { name: 'Ajuda' }).click();
	await page.getByTestId('help-tour').click();
	await expect(mark).toHaveAttribute('data-step', 'chart');
	await page.getByTestId('coach-skip').click();
	await expect(mark).toHaveCount(0);

	// Once per device: the next campaign terminal shows no coach mark.
	await page.reload();
	await page.getByTestId('campaign-continue').click();
	const open = page.getByRole('button', { name: /Abrir terminal/ });
	await expect(open.or(page.locator('.turn'))).toBeVisible();
	if (await open.count()) await open.click();
	await answerIfAsked(page);
	await expect(page.locator('.turn')).toBeVisible();
	await expect(mark).toHaveCount(0);
});

test('guided first trade reaches its debrief step after the advance step; the briefing replays it in free play', async ({ page }) => {
	await page.goto('/?seed=4242');
	await expect(page.getByTestId('campaign')).toBeVisible(); // the app created its database
	await seedProgress(page, { version: 2, tutorialsSeen: ['m06-thin-book', 'm01-false-breakout'] });
	await page.getByTestId('campaign-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	const mark = page.getByTestId('coach-mark');
	for (const s of ['chart', 'ticket', 'stop', 'risk']) {
		await expect(mark).toHaveAttribute('data-step', s);
		await page.getByTestId('coach-next').click();
	}
	await expect(mark).toHaveAttribute('data-step', 'advance');
	await answerIfAsked(page);
	await page.locator('[data-coach=advance]').click(); // one advance completes the step
	await expect(mark).toHaveCount(0);
	await playToEnd(page);
	await expect(mark).toHaveAttribute('data-step', 'debrief');
	await expect(page.locator('[data-coach=debrief]')).toHaveClass(/coach-target/);
	await page.getByTestId('coach-next').click();
	await expect(mark).toHaveCount(0);

	await page.getByRole('button', { name: /Voltar às missões/ }).first().click();
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(1).click();
	const skip = page.getByRole('button', { name: 'Pular tutorial' });
	await expect(skip.or(page.getByTestId('brief-tour'))).toBeVisible();
	if (await skip.count()) await skip.click();
	await page.getByTestId('brief-tour').click();
	await answerIfAsked(page);
	await expect(mark).toHaveAttribute('data-step', 'chart');
});
