import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';

// Builder and terminal wave 1 (add-practice-and-advanced-strategies W1-W4..W1-W8): a limit calendar on two
// expiries with its horizon analysis and heatmap, filled, rolled and adjusted with a hedge leg (one combo
// throughout); the portfolio panel and its "Hedge delta" ticket.

const M17 = 16;

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

async function advance(page: Page, n = 1) {
	for (let i = 0; i < n; i++) {
		await answerIfAsked(page);
		const turn = await page.locator('.turn').textContent();
		await page.keyboard.press('Space');
		await expect(page.locator('.turn')).not.toHaveText(turn ?? '');
	}
	await answerIfAsked(page);
}

async function start(page: Page, index: number, seed: number) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(index).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.getByRole('dialog', { name: /Pergunta de calibração/ })).toBeVisible();
	await answerIfAsked(page);
}

async function skipPlan(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Plano antes de avançar/ });
	await expect(dialog).toBeVisible();
	await dialog.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(dialog).toBeHidden();
}

/** A limit typed PT-BR style from a number. */
const pt = (v: number) => v.toFixed(2).replace('.', ',');

test('a limit calendar: horizon analysis and heatmap, filled, then rolled and adjusted with a hedge leg', async ({ page }) => {
	await start(page, M17, 3);
	await page.getByTestId('tab-builder').click();
	await page.getByTestId('builder-qty').fill('5');
	await page.getByTestId('builder-qty').press('Tab');
	// The far leg on the last listed expiry, so a roll of the near leg still leaves a calendar.
	const far = page.getByTestId('builder-expiry-far');
	await far.selectOption({ index: (await far.locator('option').count()) - 1 });
	await page.getByTestId('template-calendar_call').click();
	await expect(page.getByTestId('builder-leg')).toHaveCount(2);
	// Both legs as limits: the default is the passive mid; make them marketable at the next open.
	const types = page.getByTestId('builder-leg-type');
	for (let i = 0; i < 2; i++) await types.nth(i).selectOption('limit');
	const limits = page.getByTestId('builder-leg-limit');
	const mids = await Promise.all([0, 1].map(async (i) => Number((await limits.nth(i).inputValue()).replace(',', '.'))));
	expect(mids.every((m) => m > 0)).toBe(true);
	// Row 0 is the near short (sell), row 1 the far long (buy).
	await limits.nth(0).fill(pt(mids[0] * 0.5));
	await limits.nth(0).press('Tab');
	await limits.nth(1).fill(pt(mids[1] * 1.6));
	await limits.nth(1).press('Tab');
	await expect(page.getByTestId('builder-tif-select')).toHaveValue('ioc');
	await expect(page.getByTestId('builder-horizon')).toContainText('No primeiro vencimento');
	await expect(page.getByTestId('builder-max-loss')).toHaveText(/\d+,\d{2} cr/);
	await expect(page.getByTestId('builder-preview')).toBeVisible();
	// The heatmap: the engine's grid, read cell by cell, and a table for screen readers.
	await page.getByTestId('builder-heatmap').locator('summary').click();
	await expect(page.getByTestId('builder-heatmap-cell').first()).toBeVisible();
	await page.getByTestId('builder-heatmap-cell').nth(10).hover();
	await expect(page.getByTestId('builder-heatmap-readout')).toContainText('Preço');
	await page.getByTestId('builder-heatmap-toggle').click();
	await expect(page.getByTestId('builder-heatmap-table')).toBeVisible();
	const axe = await new AxeBuilder({ page }).include('[data-testid="builder-heatmap"]').withTags(['wcag2a', 'wcag2aa']).analyze();
	expect(axe.violations.map((v) => `${v.id} ${v.nodes[0]?.target.join(' ')} ${v.nodes[0]?.failureSummary}`)).toEqual([]);
	await page.getByTestId('builder-submit').click();
	await skipPlan(page);
	await advance(page);
	// Filled at the open: the combo is live and can be rolled.
	const live = page.getByTestId('live-combos');
	await expect(live).toBeVisible();
	await live.getByTestId('roll-c1').click();
	await expect(page.getByTestId('roll-panel')).toBeVisible();
	await expect(page.getByTestId('roll-to').locator('option').first()).toBeAttached(); // the next expiry by default
	await expect(page.getByTestId('live-orders').locator('li')).toHaveCount(2);
	await expect(page.getByTestId('live-orders').locator('li').first()).toContainText('Comprar'); // the near short bought back first
	await expect(page.getByTestId('roll-net')).toHaveText(/cr$/);
	await page.getByTestId('live-send').click();
	await skipPlan(page);
	await advance(page);
	// Adjust: a perpetual hedge leg toward delta neutral, same combo.
	await live.getByTestId('adjust-c1').click();
	await page.getByTestId('adjust-add-hedge').click();
	await expect(page.getByTestId('live-orders').locator('li')).toHaveCount(1);
	await expect(page.getByTestId('live-orders')).toContainText(/perpétuo/i);
	await page.getByTestId('live-send').click();
	await skipPlan(page);
	await expect(page.locator('.notice.ok')).toContainText('Ajustar');
	await advance(page);
	await expect(live.locator('ul').first()).toContainText(/perpétuo/i); // the hedge filled: one combo, now with a perpetual leg
});

test('portfolio: the engine totals of the book and a "Hedge delta" ticket sent after a confirmation', async ({ page }) => {
	await start(page, 0, 3); // Mission 01: perpetual only, no risk limits
	const ticket = page.locator('section.ticket');
	await ticket.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await ticket.locator('label:has-text("Quantidade") input').fill('10');
	await ticket.getByRole('button', { name: /Enviar ordem/ }).click();
	await skipPlan(page);
	await advance(page);
	await expect(page.getByTestId('rail-delta')).toHaveText(/^\+1\.0\d\d,\d{2} cr$/);
	await page.getByTestId('open-portfolio').click();
	const panel = page.getByTestId('portfolio');
	await expect(panel.getByTestId('portfolio-rows')).toContainText(/perpétuo/i);
	await expect(panel.getByTestId('portfolio-delta')).toHaveText('10,0000');
	await expect(panel.getByTestId('portfolio-no-limits')).toBeVisible();
	await expect(panel.getByTestId('portfolio-hedge-note')).toContainText('10,0');
	const axe = await new AxeBuilder({ page }).include('[data-testid="portfolio"]').withTags(['wcag2a', 'wcag2aa']).analyze();
	expect(axe.violations.map((v) => `${v.id} ${v.nodes[0]?.target.join(' ')} ${v.nodes[0]?.failureSummary}`)).toEqual([]);
	await panel.getByTestId('portfolio-hedge').click();
	const confirm = page.getByTestId('confirm-dialog');
	await expect(confirm).toContainText('Delta depois ≈ 0,0000');
	await confirm.getByTestId('confirm-yes').click();
	await expect(page.getByTestId('order-row')).toHaveCount(1); // the hedge reduces the long: no plan needed
	await advance(page);
	await expect(page.getByTestId('rail-delta')).toHaveText('0,00 cr');
});
