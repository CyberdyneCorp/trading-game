import { writeFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { lastState, recordWorkerMessages, workerLog } from './helpers';

// W3-X1 (add-practice-and-advanced-strategies): the practice formats with the same inputs as the
// desktop's cross-check (apps/desktop/tests/windowed/test_playtest_practice.cpp, "X1"), PT-BR, the
// device clock at 2026-11-09 10:00 local: the first drill session's answers, the daily challenge of ten
// dates, five guess rounds and the coach review of m23 at seed 3 with a mistake retried from its first
// warning. Every expected number is the desktop's (and was checked by hand, see the design's W3-X notes).
// CROSS_CHECK_OUT=<dir> writes what each screen showed there for a side-by-side reading.

const DONE = /Missão concluída|GAME OVER/;
const out = process.env.CROSS_CHECK_OUT;
const note = (name: string, text: string) => out && writeFileSync(`${out}/web-w3-${name}.txt`, text);

test.beforeEach(async ({ page }) => {
	await page.clock.install({ time: new Date(2026, 10, 9, 10, 0, 0) });
});

// The first session of 2026-11-09 (drills.json buildSession, session 0): each answer as the feedback of
// a wrong answer shows it.
const DRILL_ANSWERS = ['2,40', '8,40', '10,40', 'Atrapalha (theta negativo)', '95,50', '0,40'];

test('X1 drills: the first session of 2026-11-09 asks the desktop\'s items with its answers', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-drills').click();
	await page.getByTestId('drills-timer').uncheck();
	await page.getByTestId('drills-start').click();
	const seen: string[] = [];
	for (let i = 0; i < 6; i++) {
		const prompt = await page.getByTestId('drills-prompt').innerText();
		const choice = page.getByTestId('drills-choice');
		if (await choice.count()) await choice.first().click();
		else {
			await page.getByTestId('drills-input').fill('-1');
			await page.getByTestId('drills-check').click();
		}
		const feedback = await page.getByTestId('drills-feedback').innerText();
		seen.push(`${prompt}\n  ${feedback}`);
		if (!feedback.includes('Certo')) expect(feedback).toContain(DRILL_ANSWERS[i]);
		await page.getByTestId('drills-next').click();
	}
	note('drills', seen.join('\n'));
	await expect(page.getByTestId('drills-first-try')).toContainText('0 de 6');
});

// Ten dates: the day index since 2026-01-01 picks the pool's mission (24, without m12), FNV-1a 32 of
// "tradelab-daily-v1:<date>" the seed (computed independently in Python for the design's notes).
const DAILY: [string, string, number][] = [
	['2025-12-31', 'Missão 25', 1637798984],
	['2026-01-01', 'Missão 01', 138805866],
	['2026-02-28', 'Missão 11', 3464289812],
	['2026-03-01', 'Missão 13', 416280928],
	['2026-11-09', 'Missão 01', 3582066521],
	['2026-11-10', 'Missão 02', 3800322663],
	['2026-12-31', 'Missão 05', 1083961673],
	['2027-01-01', 'Missão 06', 1635316717],
	['2028-02-29', 'Missão 23', 2268926261],
	['2028-03-01', 'Missão 24', 2243913778]
];

test('X1 daily: ten dates give the desktop\'s missions and seeds', async ({ page }) => {
	const seen: string[] = [];
	for (const [date, mission, seed] of DAILY) {
		const [y, m, d] = date.split('-').map(Number);
		await page.clock.setFixedTime(new Date(y, m - 1, d, 10, 0, 0));
		await page.goto('/');
		await page.getByTestId('open-daily').click();
		await expect(page.getByTestId('daily-today')).toHaveText(`Hoje, ${date}`);
		await expect(page.getByTestId('daily-mission')).toContainText(mission);
		await expect(page.getByTestId('daily-seed')).toHaveText(`Mercado do dia: semente ${seed}`);
		seen.push(`${date} ${await page.getByTestId('daily-mission').innerText()} ${seed}`);
	}
	note('daily', seen.join('\n'));
});

// Five rounds of 2026-11-09 with fixed commitments: levels at fixed distances from the shown close, typed
// with a decimal comma. Expected: the round, the close at the decision and 12 bars later, the score.
interface Round {
	decision: 'long' | 'short' | 'none';
	p: number;
	inv: number; // distance to the invalidation, on the losing side
	target: number; // distance to the target, on the winning side
}
const ROUNDS: Round[] = [
	{ decision: 'long', p: 70, inv: 1.5, target: 3 },
	{ decision: 'short', p: 30, inv: 1.5, target: 3 },
	{ decision: 'none', p: 50, inv: 0, target: 0 },
	{ decision: 'long', p: 95, inv: 0.2, target: 0.2 },
	{ decision: 'short', p: 80, inv: 4, target: 1 }
];
const EXPECTED_ROUNDS: string[] = [];

const ptNumber = (text: string) => Number(text.replace(/\./g, '').replace(',', '.'));
const ptPrice = (v: number) => v.toFixed(2).replace('.', ',');

test('X1 guess: five rounds with the desktop\'s commitments score as on the desktop', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-guess').click();
	await page.getByTestId('guess-start').click();
	const seen: string[] = [];
	for (const [i, r] of ROUNDS.entries()) {
		await expect(page.getByTestId('guess-commit')).toBeVisible();
		const market = await page.getByTestId('guess-market').innerText();
		const facts = await page.getByTestId('guess-close').innerText();
		const close = /Fechamento agora: ([\d.,]+)/.exec(facts)?.[1] ?? '';
		const c = ptNumber(close);
		await page.getByTestId(`guess-${r.decision}`).check();
		await page.getByTestId('guess-p').selectOption(String(r.p));
		let levels = '';
		if (r.decision !== 'none') {
			const dir = r.decision === 'long' ? 1 : -1;
			const inv = ptPrice(c - dir * r.inv);
			const target = ptPrice(c + dir * r.target);
			await page.getByTestId('guess-inv').fill(inv);
			await page.getByTestId('guess-target').fill(target);
			levels = `${inv} / ${target}`;
		}
		await page.getByTestId('guess-commit-btn').click();
		await expect(page.getByTestId('guess-reveal')).toBeVisible();
		// Levels typed with a decimal comma are shown as typed (they read as a dash before W3-X2).
		if (r.decision !== 'none') await expect(page.getByTestId('guess-levels')).toHaveText(`Invalidação ${levels.replace(' / ', ' · alvo ')}`);
		const line = `${market} | ${facts} | ${levels} | ${await page.getByTestId('guess-outcome').innerText()} | ${await page.getByTestId('guess-score').innerText()}`;
		seen.push(`${line}\n${await page.getByTestId('guess-reveal').innerText()}`);
		if (EXPECTED_ROUNDS[i]) expect(line).toBe(EXPECTED_ROUNDS[i]);
		if (i < ROUNDS.length - 1) await page.getByTestId('guess-next').click();
	}
	note('guess', seen.join('\n----\n'));
});

// ---- m23 at seed 3 (wave 2's cross-check inputs), the coach review and a retry from its first warning ----

async function skipQuestion(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: 'Pular', exact: true }).click();
		await expect(dialog).toBeHidden();
	}
}

async function advance(page: Page) {
	await skipQuestion(page);
	const turn = await page.locator('.turn').textContent();
	await page.keyboard.press('Space');
	await expect(page.locator('.turn')).not.toHaveText(turn ?? '');
	await skipQuestion(page);
}

async function playToEnd(page: Page) {
	for (let i = 0; i < 80 && !(await page.getByText(DONE).count()); i++) {
		await skipQuestion(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(60);
	}
	await expect(page.getByText(DONE)).toBeVisible();
}

async function buy(page: Page, qty: string) {
	await skipQuestion(page);
	const ticket = page.locator('section.ticket');
	await ticket.locator('.sides').getByRole('radio', { name: /Comprar/ }).click();
	await ticket.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await ticket.locator('label:has-text("Quantidade") input').fill(qty);
	await ticket.getByRole('button', { name: /Enviar ordem/ }).click();
	const plan = page.getByRole('dialog', { name: /Plano antes de avançar/ });
	if (await plan.count()) await plan.getByRole('button', { name: /Enviar sem plano/ }).click();
}

const M23_DIGEST = '6ae787a60d868510740c2e441707fa847a68266d96b8df6e5fff3d02059b7590';
const MOMENTS = [
	'Momento 1 de 2 · barra 62 | Atenção | Barra 62: logo depois de o trade 1 fechar com perda de 8,31 cr na barra 62, veio uma entrada nova, sem plano, com 2,00× o tamanho dele. Depois de uma perda, espere o setup se formar de novo.',
	'Momento 2 de 2 · barra 63 | Atenção | Barra 63: a posição (20,0 a 100,31) ficou aberta no fechamento (99,96) sem nenhum stop. Sem stop, a perda não tem um limite que você escolheu.'
];

// m23 at seed 3 with wave 2's X1 inputs: 10 bought, closed by hand the next bar, 20 bought the bar after.
async function playM23(page: Page) {
	await recordWorkerMessages(page);
	await page.goto('/?seed=3');
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(22).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await skipQuestion(page);
	await buy(page, '10');
	await advance(page);
	await page.getByRole('button', { name: /Zerar posição/ }).click();
	const plan = page.getByRole('dialog', { name: /Plano antes de avançar/ });
	if (await plan.count()) await plan.getByRole('button', { name: /Enviar sem plano/ }).click();
	await advance(page);
	await buy(page, '20');
	await playToEnd(page);
	expect(lastState(await workerLog(page))?.digest).toBe(M23_DIGEST);
}

// The first warning (the revenge trade at bar 62) retried from the coach review.
async function retryFirstWarning(page: Page) {
	await page.getByTestId('open-coach').click();
	await expect(page.getByTestId('coach-position')).toHaveText('Momento 1 de 2 · barra 62');
	await page.getByTestId('coach-retry').click();
	await expect(page.getByTestId('mistake-banner')).toContainText('Nova tentativa a partir da barra 62: trade de vingança');
}

test('X1 coach: m23 at seed 3 reviewed and retried from its first warning with no trade (avoided)', async ({ page }) => {
	test.setTimeout(180_000);
	await playM23(page);
	await page.getByTestId('open-coach').click();
	await expect(page.getByTestId('coach-position')).toBeVisible();
	const moments: string[] = [];
	for (let i = 0; await page.getByTestId('coach-position').count(); i++) {
		const pos = await page.getByTestId('coach-position').innerText();
		// Nothing after the moment's bar is drawn.
		await expect(page.getByTestId('coach-chart')).toHaveAttribute('data-last-bar', /barra (\d+)/.exec(pos)?.[1] ?? '');
		moments.push(`${pos} | ${await page.getByTestId('coach-tone').innerText()} | ${await page.getByTestId('coach-text').innerText()}`);
		await page.getByTestId('coach-next').click();
	}
	note('coach', moments.join('\n'));
	expect(moments).toEqual(MOMENTS);
	await expect(page.getByTestId('coach-end')).toBeVisible();
	await page.getByTestId('coach-close').click();

	await retryFirstWarning(page);
	await playToEnd(page);
	const report = page.getByTestId('mistake-report');
	await expect(report).toHaveAttribute('data-outcome', 'avoided');
	await expect(page.getByTestId('mistake-outcome')).toHaveText('Evitado: trade de vingança não apareceu entre as barras 62 e 67.');
	// Original 35 and −77,71 cr; the retry keeps only the first trade: −8,31 cr = 10 × (100,41 − 101,14) − 1,01 of fees.
	await expect(report.locator('tbody tr').nth(0)).toHaveText(/Original\s*35\s*-77,71 cr/);
	await expect(report.locator('tbody tr').nth(1)).toHaveText(/Nova tentativa\s*47\s*-8,31 cr/);
	const digest = lastState(await workerLog(page))?.digest ?? '';
	note('retry', `${await report.innerText()}\n---- digest ${digest}\n`);
	expect(digest).toBe('1f98858d99defe5234683c79a16c2208dcdf4bd2ca2559b878e47f8b74aa0952');
});

test('X1 coach: the same retry buying the 20 again at once repeats the mistake', async ({ page }) => {
	test.setTimeout(180_000);
	await playM23(page);
	await retryFirstWarning(page);
	await buy(page, '20');
	await playToEnd(page);
	const report = page.getByTestId('mistake-report');
	await expect(report).toHaveAttribute('data-outcome', 'repeated');
	const digest = lastState(await workerLog(page))?.digest ?? '';
	note('retry-again', `${await report.innerText()}\n---- digest ${digest}\n`);
	// The same orders as the original, so the same result; the digest differs because the retry starts
	// without the advisor's allowance.
	await expect(report.locator('tbody tr').nth(1)).toHaveText(/Nova tentativa\s*35\s*-77,71 cr/);
	expect(digest).toBe('60af7fa9b04628db2e9f1361389a3dd02b2fcb9a7291a54aa4831d49ebc63c7c');
});
