import { expect, test, type Page } from '@playwright/test';
import { fileURLToPath } from 'node:url';

// A counterfactual branch pack (spec market-generator "Branch labeling"; add-scenarios-and-gameplay
// 3.3) built by tools/packer with real zstd compression: e2e/fixtures/branch.tlpack is a masked
// 150-bar CSV window (warm-up 40) whose bars after bar 90 come from m08 seed 4. It is signed with
// the device key of DEVICE_SEED, so this test makes that seed the browser's device seed first.
// Rebuild: see engine/README.md "Pack authoring".

const FIXTURE = fileURLToPath(new URL('./fixtures/branch.tlpack', import.meta.url));
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
