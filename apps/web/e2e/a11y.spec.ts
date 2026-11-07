import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';
import { answerIfAsked, marketOrder, playToEnd, startFirstMission } from './helpers.ts';

// Accessibility (spec terminal-ui accessibility requirements; task 1.1): axe finds no WCAG 2.1 A/AA
// violation on the menu, briefing, game, debrief, journal and curriculum map.

async function expectNoViolations(page: Page, screen: string) {
	// The CRT overlay is decorative (aria-hidden, pointer-events: none) and off by default.
	const { violations } = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa']).analyze();
	const summary = violations.map((v) => `${screen}: ${v.id} (${v.impact}) ${v.nodes.length}× ${v.nodes[0]?.target.join(' ')}`);
	expect(summary).toEqual([]);
}

test('menu, journal and curriculum map', async ({ page }) => {
	await page.goto('/?seed=4242');
	await expect(page.getByRole('button', { name: /Iniciar missão/ }).first()).toBeVisible();
	await expectNoViolations(page, 'menu');
	await page.getByTestId('open-journal').click();
	await expect(page.getByRole('heading', { level: 1 })).toBeVisible();
	await expectNoViolations(page, 'journal');
	await page.goto('/?seed=4242');
	await page.getByTestId('open-curriculum').click();
	await expect(page.getByRole('heading', { level: 1 })).toBeVisible();
	await expectNoViolations(page, 'curriculum map');
});

test('briefing, game and debrief', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();
	await expectNoViolations(page, 'briefing');
	await page.goto('/?seed=4242');
	await startFirstMission(page);
	await marketOrder(page);
	await page.keyboard.press('Space');
	await expect(page.getByText(/Execução #1/).first()).toBeVisible();
	await expectNoViolations(page, 'game');
	await playToEnd(page);
	await expect(page.getByText(/Revelação do mercado/i)).toBeVisible();
	await expectNoViolations(page, 'debrief');
});

test('options terminal (m05): chain, option ticket on both sides and the perpetual ticket on the sell side', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(4).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	await answerIfAsked(page);
	await page.getByRole('tab', { name: 'Opções' }).click();
	await page.locator('.chain table').first().locator('tbody tr').nth(4).locator('td').nth(3).getByRole('button').click();
	await expect(page.getByRole('button', { name: /Enviar ordem de opção/ })).toBeVisible();
	await expectNoViolations(page, 'option ticket, buy'); // the side not chosen (Sell) stays readable
	await page.locator('.chain table').first().locator('tbody tr').nth(4).locator('td').nth(2).getByRole('button').click();
	await expectNoViolations(page, 'option ticket, sell'); // the open-orders panel scrolls: it must take keyboard focus
	await page.getByRole('tab', { name: 'Perpétuo' }).click();
	await page.locator('.sides button.sell').click();
	await expectNoViolations(page, 'perpetual ticket, sell'); // the Enter hint on the red submit button
});
