import AxeBuilder from '@axe-core/playwright';
import { readFile } from 'node:fs/promises';
import { expect, test, type Browser, type Page } from '@playwright/test';
import { dumpDb, marketOrder, playToEnd, startFirstMission } from './helpers.ts';

// Run files and journal statistics (openspec add-run-sharing-and-journal-stats): a finished run
// exported, imported on a fresh "device" (browser context), watched bar by bar and debriefed without
// counting for progress; a tampered file refused; the statistics screen over the player's own runs.

async function finishRun(page: Page) {
	await startFirstMission(page);
	await marketOrder(page);
	await playToEnd(page);
}

async function exportFromDebrief(page: Page, notes = false): Promise<string> {
	if (notes) await page.getByTestId('run-export-notes').check();
	const [download] = await Promise.all([page.waitForEvent('download'), page.getByTestId('run-export-button').click()]);
	expect(download.suggestedFilename()).toMatch(/^tradelab-m\d\d-.+-\d+\.tlrun$/);
	await expect(page.getByTestId('run-export-message')).toContainText('Arquivo salvo');
	return readFile((await download.path())!, 'utf8');
}

async function freshDevice(browser: Browser): Promise<Page> {
	const page = await (await browser.newContext()).newPage();
	await page.goto('/');
	await expect(page.getByTestId('open-stats')).toBeVisible();
	return page;
}

async function importText(page: Page, text: string, name = 'run.tlrun') {
	await page.getByTestId('run-import-file').first().setInputFiles({ name, mimeType: 'application/json', buffer: Buffer.from(text) });
}

test('a finished run exported on one device replays bar by bar and debriefs on another, without counting', async ({ page, browser }) => {
	await page.goto('/?seed=21');
	await finishRun(page);
	const text = await exportFromDebrief(page);
	const file = JSON.parse(text) as { format: string; seed: number; journal: unknown[]; result: { status: string; digest: string } };
	expect(file.format).toBe('tradelab-run');
	expect(file.seed).toBe(21);
	expect(file.journal).toEqual([]); // notes only when ticked
	expect(text).not.toMatch(/run-[a-z0-9]+-/); // no run id
	const own = (await dumpDb(page)).runs as { digest: string; status: string }[];
	expect(own.find((r) => r.status === 'finished')?.digest).toBe(file.result.digest);

	const other = await freshDevice(browser);
	await importText(other, text);
	await expect(other.getByTestId('run-import-message')).toContainText('verificada pelo motor');
	await other.getByTestId('run-import-watch').click();
	await expect(other.getByTestId('replay')).toBeVisible();
	await expect(other.getByTestId('replay-bar')).toContainText(/Barra 0 de \d+/);
	await other.getByTestId('replay-next').click();
	await expect(other.getByTestId('replay-bar')).toContainText(/Barra 1 de/);
	await other.getByTestId('replay-next').click();
	await expect(other.getByTestId('replay-bar')).toContainText(/Barra 2 de/);
	await other.getByTestId('replay-end').click();
	await expect(other.getByTestId('debrief-viewing')).toBeVisible();
	await expect(other.getByTestId('debrief-points')).toHaveCount(0);
	await expect(other.getByTestId('debrief-again')).toHaveCount(0);
	// Exported again from the viewer: the very same canonical file.
	const [again] = await Promise.all([other.waitForEvent('download'), other.getByTestId('run-export-button').click()]);
	expect(JSON.parse(await readFile((await again.path())!, 'utf8'))).toEqual(JSON.parse(text));

	await other.getByTestId('debrief-leave-replay').click();
	await expect(other.getByText(/✓ Concluída/)).toHaveCount(0); // an imported run never completes a mission
	const db = await dumpDb(other);
	expect(db.runs).toEqual([]);
	expect(db.imports).toHaveLength(1);
	await other.getByTestId('open-stats').click();
	await expect(other.getByTestId('stats-empty')).toBeVisible(); // nor counts for the statistics
	await other.getByRole('button', { name: /Voltar às missões/ }).click();
	await other.getByTestId('open-journal').click();
	await expect(other.getByTestId('journal-import')).toHaveCount(1);
	await importText(other, text); // the same file again keeps one copy
	await expect(other.getByTestId('journal-import')).toHaveCount(1);
});

test('a tampered or malformed run file is refused and nothing is stored', async ({ page, browser }) => {
	await page.goto('/?seed=22');
	await finishRun(page);
	const text = await exportFromDebrief(page);
	const file = JSON.parse(text) as { result: { digest: string }; actions: { k: string; qty?: string }[] };
	const other = await freshDevice(browser);

	const forged = { ...file, result: { ...file.result, digest: '0'.repeat(64) } };
	await importText(other, JSON.stringify(forged));
	await expect(other.getByTestId('run-import-message')).toContainText('não chega ao resultado gravado');

	const order = file.actions.find((a) => a.k === 'order')!;
	order.qty = String(Number(order.qty) * 2);
	await importText(other, JSON.stringify(file));
	await expect(other.getByTestId('run-import-message')).toContainText('não chega ao resultado gravado');

	await importText(other, text.slice(0, 200));
	await expect(other.getByTestId('run-import-message')).toContainText('não é um JSON de partida válido');
	await importText(other, JSON.stringify({ ...forged, device: 'x' }));
	await expect(other.getByTestId('run-import-message')).toContainText('campo inválido: device');
	expect((await dumpDb(other)).imports).toEqual([]);
});

test('journal statistics over two own runs, watching one of them again, with axe', async ({ page }) => {
	await page.goto('/?seed=23');
	await finishRun(page);
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await finishRun(page);
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByTestId('open-stats').click();
	await expect(page.getByTestId('stats')).toBeVisible();
	await expect(page.getByTestId('stats-runs').locator('tbody tr')).toHaveCount(2);
	await expect(page.getByTestId('stats-too-few')).toBeVisible();
	await expect(page.getByTestId('stats-risk').getByTestId('stats-lesson')).toContainText('Dimensionar pelo stop');
	const { violations } = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa']).analyze();
	expect(violations.map((v) => `${v.id} ${v.nodes[0]?.target.join(' ')}`)).toEqual([]);

	await page.getByTestId('stats-watch').first().click();
	await expect(page.getByTestId('replay')).toBeVisible();
	await page.getByTestId('replay-end').click();
	await expect(page.getByTestId('debrief-viewing')).toBeVisible();
	await page.getByTestId('debrief-leave-replay').click();
	await expect(page.getByTestId('stats')).toBeVisible();
	await expect(page.getByTestId('stats-runs').locator('tbody tr')).toHaveCount(2); // watching added nothing
});
