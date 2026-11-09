import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';

// Execution policy v18 in the terminal (add-practice-and-advanced-strategies W1-W1, W1-W2, W1-W3):
// time in force on the ticket, a player-built OCO pair, and the planned trailing stop. Mission 01 at
// seed 3 (the m01-tif-oco golden's market): the first decision is bar 60 (close 101.40); bar 61 opens
// at 101.40 and trades down to 100.33; bar 63 trades from 99.70 to 101.03.

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

async function openM01(page: Page) {
	await page.goto('/?seed=3');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	// Step 60 asks a calibration question first.
	await expect(page.getByRole('dialog', { name: /Pergunta de calibração/ })).toBeVisible();
	await answerIfAsked(page);
	await expect(page.locator('.turn')).toContainText('Barra 1 de'); // decision step 60
}

const ticket = (page: Page) => page.locator('section.ticket');

async function order(page: Page, type: 'limit' | 'stop_market', price: string, side: 'buy' | 'sell' = 'buy') {
	const t = ticket(page);
	await t.getByRole('radio', { name: side === 'buy' ? /Comprar/ : /Vender/ }).click();
	await t.locator('label:has-text("Tipo") select').first().selectOption(type);
	await t.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await t.locator('label:has-text("Quantidade") input').fill('10');
	await t.locator(type === 'limit' ? 'label:has-text("Preço") input' : 'label:has-text("Gatilho") input').first().fill(price);
}

async function send(page: Page) {
	await ticket(page).getByRole('button', { name: /Enviar ordem/ }).click();
	const dialog = page.getByRole('dialog', { name: /Plano antes de avançar/ });
	if (await dialog.count()) await dialog.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.locator('.notice.ok')).toBeVisible();
}

test('time in force: an IOC limit the open does not reach is cancelled, never filled intrabar', async ({ page }) => {
	await openM01(page);
	await order(page, 'limit', '100,50');
	const tif = page.getByTestId('ticket-tif');
	// IOC/FOK are for limits: on a stop order they are disabled with the reason.
	await expect(tif.getByRole('option', { name: /Imediata ou cancela/ })).toBeEnabled();
	await page.getByTestId('ticket-tif-select').selectOption('ioc');
	await expect(page.getByTestId('ticket-tif-hint')).toContainText('Só na abertura da barra 61');
	await send(page);
	await expect(page.getByTestId('order-tif-1')).toContainText('IOC · até a barra 61');
	await advance(page);
	await expect(page.getByTestId('order-row')).toHaveCount(0);
	await expect(page.locator('section.feed')).toContainText('a validade da ordem acabou');
	await new AxeBuilder({ page }).include('section.ticket').withTags(['wcag2a', 'wcag2aa']).analyze().then((r) => expect(r.violations.map((v) => v.id)).toEqual([]));
});

test('OCO: a breakout pair on one wide bar fills one leg and cancels the other', async ({ page }) => {
	await openM01(page);
	await advance(page, 2); // bar 62, flat
	await order(page, 'stop_market', '100,90');
	await send(page);
	await order(page, 'stop_market', '99,80', 'sell');
	await send(page);
	await expect(page.getByTestId('order-row')).toHaveCount(2);
	await page.getByTestId('order-pick-1').check();
	await page.getByTestId('order-pick-2').check();
	await page.getByTestId('orders-link').click();
	const confirm = page.getByTestId('confirm-dialog');
	await expect(confirm).toContainText('mesmo se os dois preços forem tocados na mesma barra');
	await confirm.getByTestId('confirm-yes').click();
	await expect(page.getByTestId('order-link-1')).toHaveText('⛓#2');
	await expect(page.getByTestId('order-link-2')).toHaveText('⛓#1');
	await advance(page); // bar 63 trades both levels
	await expect(page.getByTestId('order-row')).toHaveCount(0);
	await expect(page.locator('section.feed')).toContainText('a outra ordem do par OCO foi executada');
});

test('trailing stop: planned in the ticket, offered after the fill, placed with one more click', async ({ page }) => {
	await openM01(page);
	const t = ticket(page);
	await t.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await t.locator('label:has-text("Quantidade") input').fill('10');
	await page.getByTestId('ticket-protect-trail').click();
	await page.getByTestId('ticket-plan-trail-mode-pct').click();
	await page.getByTestId('ticket-plan-trail-pct').fill('2');
	await expect(page.getByTestId('ticket-trail-planned')).toContainText('Nada é enviado sem o seu clique');
	await send(page);
	// Nothing trailing was sent with the entry.
	await expect(page.getByTestId('order-row')).toHaveCount(1);
	await advance(page); // the market buy fills at bar 61's open
	const notice = page.getByTestId('trail-notice');
	await expect(notice).toContainText('trailing stop de 2,00%');
	await expect(page.getByTestId('order-row')).toHaveCount(0); // still nothing sent
	await page.getByTestId('trail-notice-place').click();
	await expect(page.getByTestId('protect-tab-trail')).toHaveAttribute('aria-selected', 'true');
	await expect(page.getByTestId('protect-trail-pct')).toHaveValue('2');
	await expect(page.getByTestId('protect-trail-rule')).toContainText('Gatilho agora');
	await page.getByRole('button', { name: /Enviar stop\/alvo/ }).click();
	await expect(page.getByTestId('order-row')).toHaveCount(1);
	await expect(page.getByTestId('trail-pct')).toBeVisible();
	await expect(notice).toHaveCount(0);
});
