import { writeFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { lastState, recordWorkerMessages, workerLog } from './helpers';

// W2-X1 (add-practice-and-advanced-strategies): m18-m25 at seed 3 played with the same inputs as the
// desktop's cross-check (apps/desktop/tests/windowed/test_playtest_wave2.cpp, "X1"): calibration
// questions skipped, PT-BR. Each run must end on the state digest the desktop reaches with those inputs.
// CROSS_CHECK_OUT=<dir> writes each debrief's text there for a side-by-side reading.

const INDEX = { m18: 17, m19: 18, m20: 19, m21: 20, m22: 21, m23: 22, m24: 23, m25: 24 } as const;
const DONE = /Missão concluída|GAME OVER/;

async function skipQuestion(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: 'Pular', exact: true }).click();
		await expect(dialog).toBeHidden();
	}
}

async function advance(page: Page, n = 1) {
	for (let i = 0; i < n && !(await page.getByText(DONE).count()); i++) {
		await skipQuestion(page);
		const turn = await page.locator('.turn').textContent();
		await page.keyboard.press('Space');
		await expect(page.locator('.turn')).not.toHaveText(turn ?? '');
	}
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

async function start(page: Page, index: number) {
	await page.goto('/?seed=3');
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await skipQuestion(page);
}

async function plan(page: Page, thesis = '') {
	const dialog = page.getByRole('dialog', { name: /Plano antes de avançar/ });
	if (!(await dialog.count())) return;
	if (thesis) {
		await dialog.locator('textarea').fill(thesis);
		await dialog.getByRole('button', { name: /Registrar plano/ }).click();
	} else {
		await dialog.getByRole('button', { name: /Enviar sem plano/ }).click();
	}
	await expect(dialog).toBeHidden();
}

/** `table` = expiry, `row` = strike row, `col` 2 write call, 3 buy call, 5 write put, 6 buy put. */
async function option(page: Page, table: number, row: number, col: number, qty: string) {
	await skipQuestion(page);
	await page.locator('.chain table').nth(table).locator('tbody tr').nth(row).locator('td').nth(col).getByRole('button').click();
	await page.locator('.oticket label:has-text("Contratos") input').fill(qty);
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await plan(page);
}

/** A perpetual market order by quantity; `atr2` sets the 2×ATR stop chip. */
async function trade(page: Page, side: 'buy' | 'sell', qty: string, atr2 = false, thesis = '') {
	await skipQuestion(page);
	const perp = page.getByRole('tab', { name: 'Perpétuo' });
	if (await perp.count()) await perp.click();
	const ticket = page.locator('section.ticket');
	await ticket.locator('.sides').getByRole('radio', { name: side === 'buy' ? /Comprar/ : /Vender/ }).click();
	await ticket.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await ticket.locator('label:has-text("Quantidade") input').fill(qty);
	if (atr2) await ticket.getByTestId('ticket-stop').getByRole('button', { name: '2×ATR', exact: true }).click();
	await ticket.getByRole('button', { name: /Enviar ordem/ }).click();
	await plan(page, thesis);
}

async function rehedge(page: Page) {
	await skipQuestion(page);
	await page.getByTestId('open-portfolio').click();
	const hedge = page.getByTestId('portfolio-hedge');
	if (await hedge.isDisabled()) {
		await page.getByTestId('portfolio-close').click();
		return;
	}
	await hedge.click();
	await page.getByTestId('confirm-dialog').getByTestId('confirm-yes').click();
	await plan(page);
}

async function flat(page: Page) {
	return (await page.getByRole('button', { name: /Zerar posição/ }).count()) === 0;
}

type Script = (page: Page) => Promise<void>;

const RUNS: { mission: keyof typeof INDEX; digest: string; script: Script }[] = [
	{
		mission: 'm18',
		digest: '3e7b8802d6ab84e3e34c0606b9c6eca14702d9020e454478ed7f0309825c32a4',
		script: async (page) => {
			await option(page, 0, 4, 3, '10');
			await option(page, 0, 4, 6, '10');
			await advance(page, 8);
			await rehedge(page);
		}
	},
	{
		mission: 'm19',
		digest: '2a4dff63b1f3bc0c808f61ae416564df9d1164df215075c91e4fc08e9403b026',
		script: async (page) => {
			await option(page, 1, 4, 3, '10');
			await option(page, 1, 4, 5, '10');
			await trade(page, 'sell', '10');
		}
	},
	{
		mission: 'm20',
		digest: 'c2e382a65e9af855993b932de82e2d631fbd58e2ea651ea6802655ab2928bfaf',
		script: async (page) => {
			await option(page, 0, 5, 3, '10');
			await option(page, 0, 5, 6, '10');
		}
	},
	{
		mission: 'm21',
		digest: 'ed64a29c0900311fb05e4a304fe03acbd50b6e829e797e4c0ca4691912e0c030',
		script: async (page) => {
			await option(page, 1, 5, 3, '10');
			await option(page, 0, 5, 2, '10');
		}
	},
	{
		mission: 'm22',
		digest: '1dbad258dc19df31fa094676b686484ad61d45e79c55f51bca00ae39fe879b7c',
		script: async (page) => {
			await trade(page, 'buy', '10', true, 'tendência');
			await advance(page, 5);
			await trade(page, 'buy', '5', true, 'segunda parte');
			await advance(page, 10);
			if (!(await flat(page))) await trade(page, 'sell', '7');
		}
	},
	{
		mission: 'm23',
		digest: '6ae787a60d868510740c2e441707fa847a68266d96b8df6e5fff3d02059b7590',
		script: async (page) => {
			await trade(page, 'buy', '10');
			await advance(page);
			await page.getByRole('button', { name: /Zerar posição/ }).click();
			await plan(page);
			await advance(page);
			await trade(page, 'buy', '20');
		}
	},
	{
		mission: 'm24',
		digest: '1e27c1aedf5713d624ba9d69a3c4a12448b5c70a61079b85c7c1fa81d67714a6',
		script: async (page) => {
			await advance(page, 38);
			await trade(page, 'buy', '20');
		}
	},
	{
		mission: 'm25',
		digest: '41a46c9c7dae348e6b9859eef38f5681ac775dadc3ed47ea51c97b0738a318f4',
		script: async (page) => {
			await trade(page, 'buy', '10');
			for (let k = 0; k < 2; k++) {
				await advance(page, 12);
				await trade(page, 'buy', '20');
			}
		}
	}
];

for (const run of RUNS) {
	test(`X1 ${run.mission} at seed 3: the desktop's inputs end on its digest`, async ({ page }) => {
		test.setTimeout(120_000);
		await recordWorkerMessages(page);
		await start(page, INDEX[run.mission]);
		await run.script(page);
		await playToEnd(page);
		const digest = lastState(await workerLog(page))?.digest ?? '';
		const out = process.env.CROSS_CHECK_OUT;
		if (out) writeFileSync(`${out}/web-${run.mission}.txt`, `${await page.locator('body').innerText()}\n---- digest ${digest}\n`);
		expect(digest).toBe(run.digest);
	});
}
