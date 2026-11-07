#!/usr/bin/env node
// Regenerates the README screenshots of the web game into docs/screenshots/web/ (1440x900, English,
// CRT on, fixed seeds so the images are reproducible). Serve a build first, then run:
//   npm run build && npx vite preview --port 4173 &
//   node scripts/screenshots.mjs [http://localhost:4173]
// With Python and Pillow installed, each PNG is reduced to a 256-colour palette to keep it small.

import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const BASE = process.argv[2] ?? process.env.BASE ?? 'http://localhost:4173';
const OUT = fileURLToPath(new URL('../../../docs/screenshots/web/', import.meta.url));
const VIEWPORT = { width: 1440, height: 900 };
const SETTINGS = JSON.stringify({ crt: true, colorblind: false, locale: 'en' });

async function newPage(browser, viewport = VIEWPORT) {
	const ctx = await browser.newContext({ viewport, reducedMotion: 'no-preference' });
	await ctx.addInitScript((s) => localStorage.setItem('tradelab.settings.v1', s), SETTINGS);
	const page = await ctx.newPage();
	page.setDefaultTimeout(15_000);
	return page;
}

async function answerIfAsked(page) {
	const dialog = page.getByRole('dialog', { name: /Calibration question/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: 'Answer' }).click();
		await dialog.waitFor({ state: 'hidden' });
	}
}

async function skipTour(page) {
	const skip = page.getByRole('button', { name: /Skip tour/ });
	if (await skip.count()) await skip.click();
}

/** Opens mission `index` (0 = m01) in free play, past its tutorial, and returns on the briefing. */
async function briefing(page, seed, index) {
	await page.goto(`${BASE}/?seed=${seed}`);
	await page.getByRole('button', { name: 'Start mission' }).nth(index).click();
	await page.getByRole('button', { name: 'Skip tutorial' }).click();
	await page.getByRole('button', { name: /Open terminal/ }).waitFor();
}

async function terminal(page, seed, index) {
	await briefing(page, seed, index);
	await page.getByRole('button', { name: /Open terminal/ }).click();
	await page.locator('.turn').waitFor();
	await answerIfAsked(page);
	await skipTour(page);
}

async function advance(page, n, key = 'Space') {
	for (let i = 0; i < n; i++) {
		await answerIfAsked(page);
		await page.keyboard.press(key);
		await page.waitForTimeout(80);
	}
	await answerIfAsked(page);
}

/** A long on m01 with a 2% stop and a 2R target, planned, filled and a few bars on. */
async function m01Trade(page) {
	await terminal(page, 1001, 0);
	await page.locator('label:has-text("Leverage") input').first().fill('3');
	await page.getByTestId('ticket-stop').getByRole('button', { name: '2%', exact: true }).click();
	await page.getByTestId('ticket-tp').getByRole('button', { name: '2R', exact: true }).click();
	await page.getByRole('button', { name: /Submit order/ }).click();
	await page.locator('textarea').fill('Range floor holds: long with the stop under it, target at 2R.');
	await page.getByRole('button', { name: /Record plan and submit/ }).click();
	await advance(page, 4);
	await page.evaluate(() => (document.activeElement instanceof HTMLElement ? document.activeElement.blur() : null));
	await page.evaluate(() => window.scrollTo(0, 0));
}

async function shoot(page, name) {
	await page.waitForTimeout(400);
	await page.screenshot({ path: `${OUT}${name}.png` });
	console.log(`${name}.png`);
}

const SHOTS = [
	['01-menu', async (page) => page.goto(`${BASE}/?seed=1001`).then(() => page.getByText('Start campaign').waitFor())],
	['02-briefing', (page) => briefing(page, 1001, 2)],
	['03-terminal-m01', m01Trade],
	[
		'04-options-chain-m05',
		async (page) => {
			await terminal(page, 4242, 4);
			await page.getByRole('tab', { name: 'Options' }).click();
			await page.locator('.chain table').first().locator('tbody tr').nth(4).locator('td').nth(2).getByRole('button').click();
		}
	],
	[
		'05-strategy-builder-m16',
		async (page) => {
			await terminal(page, 1616, 15);
			await page.getByTestId('tab-builder').click();
			await page.getByTestId('template-iron_condor').click();
			await page.getByTestId('builder-payoff').waitFor();
			await page.getByTestId('builder-payoff').evaluate((el) => el.scrollIntoView({ block: 'end' }));
		}
	],
	[
		'06-whatif-lab',
		async (page) => {
			await page.goto(`${BASE}/?seed=1001`);
			await page.getByTestId('open-lab').click();
			await page.getByTestId('lab-price').evaluate((el) => {
				el.value = '8';
				el.dispatchEvent(new Event('input', { bubbles: true }));
			});
		}
	],
	[
		'07-debrief',
		async (page) => {
			await m01Trade(page);
			await page.keyboard.press('x');
			for (let i = 0; i < 40 && !(await page.getByText(/Mission complete|GAME OVER/).count()); i++) await advance(page, 1, 'Shift+Space');
			await page.getByText(/Mission complete|GAME OVER/).waitFor();
		}
	],
	[
		'08-academy-lesson',
		async (page) => {
			await page.goto(`${BASE}/`);
			await page.getByTestId('open-curriculum').click();
			await page.locator('[data-lesson=options-payoff]').getByTestId('lesson-start').click();
			for (let i = 0; i < 2; i++) await page.getByTestId('lesson-next').click();
		}
	],
	['09-curriculum', async (page) => page.goto(`${BASE}/`).then(() => page.getByTestId('open-curriculum').click())],
	[
		'10-glossary',
		async (page) => {
			await page.goto(`${BASE}/`);
			await page.getByTestId('open-glossary').click();
			await page.getByRole('searchbox').first().fill('liquidation');
		}
	]
];

mkdirSync(OUT, { recursive: true });
const browser = await chromium.launch();
for (const [name, setup] of SHOTS) {
	const page = await newPage(browser);
	await setup(page);
	await shoot(page, name);
	await page.context().close();
}
const phone = await newPage(browser, { width: 390, height: 844 });
await m01Trade(phone);
await shoot(phone, '11-phone-terminal');
await browser.close();

try {
	const quantize = 'import sys,glob\nfrom PIL import Image\nfor f in glob.glob(sys.argv[1]+"*.png"):\n Image.open(f).convert("RGB").quantize(256,dither=Image.Dither.NONE).save(f,optimize=True)';
	execFileSync('python3', ['-c', quantize, OUT], { stdio: 'inherit' });
} catch {
	console.log('Pillow not available: PNGs left unquantized');
}
