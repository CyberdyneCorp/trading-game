import { expect, test, type Page } from '@playwright/test';
import { fileURLToPath } from 'node:url';

// A counterfactual branch pack (spec market-generator "Branch labeling"; add-scenarios-and-gameplay
// 3.3) built by tools/packer with real zstd compression: e2e/fixtures/branch.tlpack is a masked
// 150-bar CSV window (warm-up 40) whose bars after bar 90 come from m08 seed 4. It is signed with
// the device key of DEVICE_SEED, so this test makes that seed the browser's device seed first.
// Rebuild: see engine/README.md "Pack authoring".

// e2e/fixtures/synthetic-m09.tlpack is `packer.py generate m09-event-straddle --seed 7` built the same
// way: a synthetic pack whose events.bin (the announced rate decision) and options.bin (two expiries)
// are zstd frames.

const FIXTURE = fileURLToPath(new URL('./fixtures/branch.tlpack', import.meta.url));
const SYNTHETIC = fileURLToPath(new URL('./fixtures/synthetic-m09.tlpack', import.meta.url));
const DEVICE_SEED = '0e2e'.repeat(16);

async function useDeviceSeed(page: Page) {
	await page.goto('/?seed=11');
	await expect(page.getByTestId('pack-file')).toBeAttached(); // the menu is up: the app has created its database
	await page.evaluate(
		(hex) =>
			new Promise<void>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onerror = () => reject(open.error);
				open.onsuccess = () => {
					try {
						const tx = open.result.transaction('meta', 'readwrite');
						tx.objectStore('meta').put({ key: 'deviceSeed', hex });
						tx.oncomplete = () => {
							open.result.close();
							resolve();
						};
						tx.onerror = () => reject(tx.error);
					} catch (e) {
						reject(e);
					}
				};
			}),
		DEVICE_SEED
	);
	await page.reload();
}

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

test('a branch pack shows the synthetic-branch label from the first generated bar on', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await useDeviceSeed(page);
	await page.getByTestId('pack-file').setInputFiles(FIXTURE);
	await expect(page.getByTestId('pack-message')).toContainText('Pacote instalado: branch');
	await expect(page.getByTestId('pack-row')).toHaveCount(1);

	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	await answerIfAsked(page);
	await expect(page.getByTestId('branch-tag')).toHaveCount(0); // recorded history only so far

	// Turn 51 shows bar 90 (the branch point); turn 52 shows the first generated bar.
	for (let i = 0; i < 60 && (await page.getByTestId('branch-tag').count()) === 0; i++) {
		await answerIfAsked(page);
		await expect(page.getByTestId('branch-tag')).toHaveCount(0);
		await page.keyboard.press('Space');
		await page.waitForTimeout(40);
	}
	await expect(page.locator('.turn')).toContainText('Barra 52');
	await expect(page.getByTestId('branch-tag')).toHaveText('RAMO SINTÉTICO');
	await expect(page.getByTestId('branch-tag')).toHaveAttribute('title', /A partir da barra 52/);
	await expect(page.locator('body')).not.toContainText('BTC/USDT'); // the reveal still waits for the debrief
	expect(errors).toEqual([]);
});

/** Clicks a quote in the option chain: `table` = expiry (0 first), `row` = strike row, `col` = 3 buy call, 6 buy put. */
function quote(page: Page, table: number, row: number, col: number) {
	return page.locator('.chain table').nth(table).locator('tbody tr').nth(row).locator('td').nth(col).getByRole('button');
}

test('a packer-built synthetic pack plays with its calendar and options; the debrief says it was generated; play again keeps the pack', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await useDeviceSeed(page);
	await page.getByTestId('pack-file').setInputFiles(SYNTHETIC);
	await expect(page.getByTestId('pack-message')).toContainText('Pacote instalado: synthetic-m09');
	// Regression: the menu row and the top bar tagged a generated pack CONHECIDO.
	await expect(page.getByTestId('pack-tag')).toHaveText('SINTÉTICO');
	await page.getByTestId('pack-start').click();
	// Regression: a generated pack was announced as an imported, masked real market.
	await expect(page.getByText(/Pacote sintético — preços gerados, não um ativo real/)).toBeVisible();
	await expect(page.getByText(/Mercado importado/)).toHaveCount(0);
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await expect(page.getByTestId('news-upcoming')).toContainText(/Decisão de juros na barra 75 · consenso: neutro/); // events.bin
	await expect(page.locator('.chain table')).toHaveCount(2); // options.bin: two expiries
	await expect(page.getByTestId('market-tag')).toHaveText('SINTÉTICO');
	await expect(page.locator('.chain table').first().locator('tbody tr').nth(3)).toContainText('97,50');

	await quote(page, 0, 3, 3).click();
	// Regression: the 97,50 strike was rounded to "C 98" in the ticket, the feed and the timeline.
	await expect(page.locator('.oticket .series')).toContainText('C 97,50 · exp 85');
	await page.getByRole('button', { name: /Enviar ordem de opção/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	for (let i = 0; i < 70 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await answerIfAsked(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(60);
	}
	await expect(page.getByText(/Missão concluída/)).toBeVisible();
	// Regression: the reveal read "O mercado K6A era — (—), de a ()." with an empty "Transformação: .".
	await expect(page.getByTestId('pack-reveal')).toContainText('foi gerado sinteticamente e distribuído em um pacote');
	await expect(page.getByText(/Transformação:/)).toHaveCount(0);
	await expect(page.locator('.timeline')).toContainText('C 97,50 · exp 85');
	await expect(page.locator('.timeline')).not.toContainText('C 98 ');

	// Regression: "play again" asked the engine for mission "m09-event-straddle-s7" and left a blank page.
	await page.getByRole('button', { name: /Jogar de novo \(mesmo pacote\)/ }).click();
	await expect(page.getByText(/Pacote sintético — preços gerados/)).toBeVisible();
	await expect(page.getByText('Treino · Véspera (pacote sintético)').first()).toBeVisible();
	expect(errors).toEqual([]);
});
