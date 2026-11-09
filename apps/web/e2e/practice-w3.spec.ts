import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';
import { answerIfAsked, marketOrder, playToEnd, startFirstMission } from './helpers';

// Wave 3 practice formats (add-practice-and-advanced-strategies D15-D19): the menu entries, a drill
// session, the daily challenge twice on a mocked date (first and best), five guess rounds, the coach
// review and a mistake retried with the assisted label. Axe on every new screen.
// SHOTS=<dir> writes screenshots of each screen there (1024 and 1440 are the viewport of the run).

const shots = process.env.SHOTS;
const width = Number(process.env.SHOT_WIDTH || 0);
if (width) test.use({ viewport: { width, height: 900 } });

async function axe(page: Page) {
	const { violations } = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa']).analyze();
	expect(violations.map((v) => `${v.id}: ${v.nodes.map((n) => n.target).join(' ')}`)).toEqual([]);
}

async function shot(page: Page, name: string) {
	if (shots) await page.screenshot({ path: `${shots}/${name}-${page.viewportSize()?.width}.png`, fullPage: true });
}

test.beforeEach(async ({ page }) => {
	// A fixed local date: the daily challenge and the drills follow the device's calendar day.
	await page.clock.install({ time: new Date(2026, 10, 9, 10, 0, 0) });
});

test('practice formats in the menu, reachable by keyboard and named for screen readers', async ({ page }) => {
	await page.goto('/');
	const nav = page.getByRole('navigation', { name: 'Prática' });
	for (const name of ['Exercícios do dia', 'Desafio do dia', 'Qual o próximo movimento?']) {
		await expect(nav.getByRole('button', { name: new RegExp(name.replace('?', '\\?')) })).toBeVisible();
	}
	await nav.getByRole('button', { name: /Exercícios do dia/ }).focus();
	await page.keyboard.press('Enter');
	await expect(page.getByRole('heading', { name: 'Exercícios do dia' })).toBeVisible();
	// No round market in the mission menu (design D19).
	await page.getByRole('button', { name: 'Voltar ao menu' }).click();
	await expect(page.getByTestId('mission-card')).toHaveCount(25);
});

test('a drill session: a wrong answer shows the worked explanation, the end screen links each lesson', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-drills').click();
	await axe(page);
	await page.getByTestId('drills-timer').uncheck(); // the accessibility setting: no clock
	await page.getByTestId('drills-start').click();
	await expect(page.getByTestId('drills-clock')).toHaveText('sem relógio');
	for (let i = 0; i < 6; i++) {
		await expect(page.getByTestId('drills-position')).toContainText(`Pergunta ${i + 1} de 6`);
		if (i === 0) await shot(page, 'drill-item');
		const choice = page.getByTestId('drills-choice');
		if (await choice.count()) await choice.last().click();
		else {
			await page.getByTestId('drills-input').fill(i === 0 ? '-999' : '0');
			await page.getByTestId('drills-check').click();
		}
		const feedback = page.getByTestId('drills-feedback');
		await expect(feedback).toBeVisible();
		if (i === 0) {
			await expect(feedback).toContainText('Não é isso: a resposta é');
			await expect(page.getByTestId('drills-explain')).toBeVisible();
			await shot(page, 'drill-explain');
			await axe(page);
		}
		await page.getByTestId('drills-next').click();
	}
	await expect(page.getByTestId('drills-end')).toBeVisible();
	await expect(page.getByTestId('drills-first-try')).toContainText('de 6 certas na primeira tentativa');
	await expect(page.getByTestId('drills-lesson').first()).toBeVisible();
	await shot(page, 'drill-end');
	await axe(page);
	await page.getByTestId('drills-lesson').first().click();
	await expect(page.locator('main')).toBeVisible();
});

async function playDaily(page: Page, buy: boolean) {
	await page.getByTestId('daily-play').click();
	const skip = page.getByRole('button', { name: /Pular tutorial|Abrir terminal/ }).first();
	if ((await skip.innerText()).includes('Pular tutorial')) await skip.click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.getByTestId('daily-banner')).toContainText('Desafio do dia 2026-11-09');
	await answerIfAsked(page);
	if (buy) await marketOrder(page);
	await playToEnd(page);
	await expect(page.getByTestId('debrief-daily')).toBeVisible();
}

test('the daily challenge twice on a mocked date: the first attempt is the record, the best beside it', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-daily').click();
	await expect(page.getByTestId('daily-today')).toHaveText('Hoje, 2026-11-09');
	// 2026-11-09 is day 312 since 2026-01-01; 312 mod 24 = 0: the pool's first mission.
	await expect(page.getByTestId('daily-mission')).toContainText('Missão 01');
	await expect(page.getByTestId('daily-history')).toContainText('Nenhum desafio terminado ainda');
	await shot(page, 'daily');
	await axe(page);
	await playDaily(page, true);
	await page.getByRole('button', { name: 'Voltar às missões' }).click();
	await page.getByTestId('open-daily').click();
	await expect(page.getByTestId('daily-row')).toHaveCount(1);
	const first = await page.getByTestId('daily-first').innerText();
	await playDaily(page, false);
	await page.getByRole('button', { name: 'Voltar às missões' }).click();
	await page.getByTestId('open-daily').click();
	await expect(page.getByTestId('daily-row')).toHaveCount(1);
	await expect(page.getByTestId('daily-first')).toHaveText(first);
	await expect(page.getByTestId('daily-row').locator('td.num')).toHaveText('2');
	await shot(page, 'daily-history');
});

test('five guess rounds: no bar after the decision before the commitment, then the reveal and the score', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-guess').click();
	await axe(page);
	await page.getByTestId('guess-start').click();
	for (let r = 0; r < 5; r++) {
		await expect(page.getByTestId('guess-commit')).toBeVisible();
		const market = await page.getByTestId('guess-market').innerText();
		const step = Number(/barra (\d+)/.exec(market)?.[1]);
		// The chart's screen-reader table lists the bars the client holds: none after the decision.
		const close = /Fechamento agora: ([\d.,]+)/.exec(await page.getByTestId('guess-close').innerText())?.[1] ?? '100';
		const c = Number(close.replace(/\./g, '').replace(',', '.'));
		if (r === 0) {
			await shot(page, 'guess-commit');
			await axe(page);
		}
		if (r % 3 === 2) await page.getByTestId('guess-none').check();
		else {
			await page.getByTestId(r % 3 === 0 ? 'guess-long' : 'guess-short').check();
			const d = r % 3 === 0 ? 1 : -1;
			await page.getByTestId('guess-inv').fill((c - d * c * 0.01).toFixed(2));
			await page.getByTestId('guess-target').fill((c + d * c * 0.02).toFixed(2));
		}
		await page.getByTestId('guess-p').selectOption(String(r % 3 === 0 ? 70 : r % 3 === 1 ? 30 : 50));
		await page.getByTestId('guess-commit-btn').click();
		await expect(page.getByTestId('guess-reveal')).toBeVisible();
		await expect(page.getByTestId('guess-score')).toContainText(/Nota [\d,]+ de 100/);
		await expect(page.getByTestId('guess-one-sample')).toBeVisible();
		expect(step).toBeGreaterThanOrEqual(80);
		if (r === 0) {
			await shot(page, 'guess-reveal');
			await axe(page);
		}
		if (r < 4) await page.getByTestId('guess-next').click();
	}
	await expect(page.getByTestId('guess-history')).toContainText('Rodadas jogadas: 5');
	await expect(page.getByTestId('guess-history')).toContainText('A tabela de calibração aparece depois de 20 rodadas (até agora: 5)');
	const stored = await page.evaluate(
		() =>
			new Promise<number>((resolve) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => {
					const get = open.result.transaction('meta').objectStore('meta').get('progress');
					get.onsuccess = () => resolve((get.result?.guessHistory ?? []).length);
				};
			})
	);
	expect(stored).toBe(5);
});

test('coach review with the chart cut at each moment, then a mistake retried with the assisted label', async ({ page }) => {
	await page.goto('/?seed=3');
	await startFirstMission(page);
	await marketOrder(page); // a long with no stop: the coach flags it once it fills
	await playToEnd(page);
	await page.getByTestId('open-coach').click();
	const dialog = page.getByRole('dialog', { name: 'Revisão com o coach' });
	await expect(dialog).toBeVisible();
	await expect(page.getByTestId('coach-position')).toBeVisible();
	const pos = await page.getByTestId('coach-position').innerText();
	const step = Number(/barra (\d+)/.exec(pos)?.[1]);
	await expect(page.getByTestId('coach-chart')).toHaveAttribute('data-last-bar', String(step)); // Future hidden at a moment
	await expect(page.getByTestId('coach-text')).toContainText(`Barra ${step}:`);
	await expect(page.getByTestId('coach-reveal')).toHaveCount(0); // the reveal is linked only after the last moment
	await shot(page, 'coach');
	await axe(page);
	await expect(page.getByTestId('coach-tone')).toHaveText('Atenção');
	await page.getByTestId('coach-retry').click();
	await expect(page.getByTestId('mistake-banner')).toContainText(`Nova tentativa a partir da barra ${step}`);
	await expect(page.getByTestId('assisted-tag')).toBeVisible();
	await shot(page, 'mistake-banner');
	await playToEnd(page);
	await expect(page.getByTestId('debrief-assisted')).toBeVisible();
	const report = page.getByTestId('mistake-report');
	await expect(report).toBeVisible();
	await expect(report).toHaveAttribute('data-outcome', /repeated|avoided/);
	await expect(report).toContainText('Uma amostra');
	await shot(page, 'mistake-report');
	await axe(page);
});

test('a returning player (progress v3, a lesson review due): the drills start with it, the practice keys are added', async ({ page }) => {
	await page.goto('/');
	await expect(page.getByTestId('open-drills')).toBeVisible();
	// A version 3 row: no practice keys; options-payoff completed, its review due today.
	await page.evaluate(
		() =>
			new Promise<void>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => {
					const tx = open.result.transaction('meta', 'readwrite');
					tx.objectStore('meta').put({
						key: 'progress',
						version: 3,
						tutorialsSeen: ['m01-false-breakout'],
						advisorPoints: 7,
						settledRuns: [],
						lessonsDone: ['options-payoff'],
						lessonReviews: { 'options-payoff': { box: 1, due: '2026-11-09' } },
						campaign: null,
						campaignHistory: []
					});
					tx.oncomplete = () => resolve();
					tx.onerror = () => reject(tx.error);
				};
			})
	);
	await page.reload();
	await page.getByTestId('open-drills').click();
	await page.getByTestId('drills-start').click();
	// The due lesson's skills come first (payoff or breakeven).
	await expect(page.getByTestId('drills-position')).toContainText(/· (Payoff|Empate)$/);
	for (let i = 0; i < 6; i++) {
		const choice = page.getByTestId('drills-choice');
		if (await choice.count()) await choice.first().click();
		else {
			await page.getByTestId('drills-input').fill('0');
			await page.getByTestId('drills-check').click();
		}
		await page.getByTestId('drills-next').click();
	}
	await expect(page.getByTestId('drills-end')).toBeVisible();
	const stored = await page.evaluate(
		() =>
			new Promise<Record<string, unknown>>((resolve) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => {
					const get = open.result.transaction('meta').objectStore('meta').get('progress');
					get.onsuccess = () => resolve(get.result);
				};
			})
	);
	expect(stored.version).toBe(4);
	expect(stored.advisorPoints).toBe(7);
	expect(stored.lessonsDone).toEqual(['options-payoff']);
	// A drill never completes the lesson's review: it is still due today.
	expect(stored.lessonReviews).toEqual({ 'options-payoff': { box: 1, due: '2026-11-09' } });
	expect((stored.drillHistory as unknown[]).length).toBe(1);
	expect(Object.keys(stored.drillReviews as object).length).toBeGreaterThan(0);
});

test('English screens (screenshots for review only)', async ({ page }) => {
	test.skip(!shots, 'SHOTS not set');
	await page.addInitScript(() => localStorage.setItem('tradelab.settings.v1', JSON.stringify({ crt: false, colorblind: false, locale: 'en' })));
	await page.goto('/?seed=3');
	await shot(page, 'en-menu');
	await page.getByTestId('open-drills').click();
	await page.getByTestId('drills-start').click();
	const choice = page.getByTestId('drills-choice');
	if (await choice.count()) await choice.last().click();
	else {
		await page.getByTestId('drills-input').fill('-999');
		await page.getByTestId('drills-check').click();
	}
	await shot(page, 'en-drill-explain');
	await page.getByTestId('drills-back').click();
	await page.getByTestId('open-daily').click();
	await shot(page, 'en-daily');
	await page.getByTestId('daily-back').click();
	await page.getByTestId('open-guess').click();
	await page.getByTestId('guess-start').click();
	await page.getByTestId('guess-none').check();
	await shot(page, 'en-guess-commit');
	await page.getByTestId('guess-commit-btn').click();
	await shot(page, 'en-guess-reveal');
	await page.getByTestId('guess-back').click();
	await page.getByRole('button', { name: /Start mission/ }).first().click();
	const next = page.getByRole('button', { name: /Skip tutorial|Open terminal/ }).first();
	if ((await next.innerText()).includes('Skip')) await next.click();
	await page.getByRole('button', { name: /Open terminal/ }).click();
	const q = page.getByRole('dialog', { name: /Calibration question/ });
	if (await q.count()) await q.getByRole('button', { name: /Answer/ }).click();
	await page.locator('label:has-text("Size by") select').selectOption('qty');
	await page.getByRole('button', { name: /Submit order/ }).click();
	await page.getByRole('button', { name: /Submit without a plan/ }).click();
	for (let i = 0; i < 80 && !(await page.getByText(/Mission complete|GAME OVER/).count()); i++) {
		const d = page.getByRole('dialog', { name: /Calibration question/ });
		if (await d.count()) await d.getByRole('button', { name: /Answer/ }).click();
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(40);
	}
	await page.getByTestId('open-coach').click();
	await shot(page, 'en-coach');
	await page.getByTestId('coach-retry').click();
	await shot(page, 'en-mistake-banner');
});
