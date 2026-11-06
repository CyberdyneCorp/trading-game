import { expect, test, type Page } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import { answerIfAsked, lastState, recordWorkerMessages, useDeviceSeed, workerLog } from './helpers.ts';

// Pack schema 2 (add-scenarios-and-gameplay 1.5): e2e/fixtures/mark-v2.tlpack is m07 seed 7 with its
// own texts, objectives and margin tiers and a mark-price series 0.25% below the last price
// (fixtures/make-mark-v2.py, signed with the device key of FIXTURE_DEVICE_SEED).

const MARK_PACK = fileURLToPath(new URL('./fixtures/mark-v2.tlpack', import.meta.url));

const price = (text: string) => Number(text.replace(/\./g, '').replace(',', '.'));

async function startMarkPack(page: Page) {
	await useDeviceSeed(page);
	await page.getByTestId('pack-file').setInputFiles(MARK_PACK);
	await expect(page.getByTestId('pack-message')).toContainText('Pacote instalado');
	await page.getByTestId('pack-start').click();
}

/** Last traded close and mark of the latest state the worker sent. */
async function prices(page: Page): Promise<{ last: number; mark: number }> {
	const s = lastState(await workerLog(page)) as unknown as { bars: string[][]; account: { mark: string } };
	return { last: Number(s.bars[s.bars.length - 1][3]), mark: Number(s.account.mark) };
}

test('a v2 pack briefs with its own texts, objectives, tiers and the mark-price note, in both locales', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await startMarkPack(page);
	await expect(page.getByRole('heading', { name: 'Treino v2 · preço de marcação' })).toBeVisible();
	await expect(page.locator('main')).toContainText('A margem deste mercado segue o preço de marcação');
	await expect(page.getByTestId('brief-objective')).toHaveCount(4);
	await expect(page.getByTestId('brief-objective').nth(1)).toContainText('15%');
	await expect(page.getByTestId('brief-objective').nth(2)).toContainText('0,75%');
	await expect(page.getByTestId('brief-tiers')).toHaveText('≥ 20.000: 2,0% (até 4x) · ≥ 60.000: 5,0% (até 2x)');
	await expect(page.getByTestId('brief-mark')).toContainText('usam o preço de marcação; as execuções usam o último preço');
	await expect(page.getByTestId('brief-shocks')).toHaveCount(0);

	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	await page.getByRole('button', { name: 'EN', exact: true }).click();
	await page.getByTestId('pack-start').click();
	await expect(page.getByRole('heading', { name: 'Practice v2 · mark price' })).toBeVisible();
	await expect(page.getByTestId('brief-tiers')).toHaveText('≥ 20,000: 2.0% (up to 4x) · ≥ 60,000: 5.0% (up to 2x)');
	await expect(page.getByTestId('brief-mark')).toContainText('use the mark price; fills use the last price');
	expect(errors).toEqual([]);
});

// Regression: with a mark-price series the ticket priced orders at the mark. The engine places,
// checks and fills orders at the last price (only margin, liquidation and funding use the mark), so
// the market entry estimate sat below the last close for a buy, and a protective stop between the
// mark and the last price was refused as "on the wrong side" although the engine takes it.
test('with a mark-price series the ticket prices orders at the last price, not the mark', async ({ page }) => {
	await recordWorkerMessages(page);
	await startMarkPack(page);
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	const { last, mark } = await prices(page);
	expect(mark).toBeLessThan(last); // the fixture's mark sits 0.25% below the last price

	const ticket = page.locator('section.ticket');
	await ticket.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	expect(price(await page.getByTestId('ticket-est-entry').innerText())).toBeGreaterThan(last);
	const type = ticket.locator('label:has-text("Tipo") select');
	await type.selectOption('limit');
	await expect(ticket.locator('label:has-text("Preço") input')).toHaveAttribute('placeholder', last.toFixed(2).replace('.', ','));
	await type.selectOption('market');

	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await page.keyboard.press('Space');
	await answerIfAsked(page);
	await expect(page.getByTestId('protect-stop')).toBeVisible();
	const now = await prices(page);
	// Below the last price (a valid stop for the long) but above the mark.
	const between = (Math.floor(((now.last + now.mark) / 2) * 100) / 100).toFixed(2).replace('.', ',');
	await page.getByTestId('protect-stop').locator('input').fill(between);
	await expect(page.getByRole('button', { name: /Enviar stop\/alvo/ })).toBeEnabled();
	await page.getByRole('button', { name: /Enviar stop\/alvo/ }).click();
	await expect(page.locator('body')).toContainText(/STOP/);
});
