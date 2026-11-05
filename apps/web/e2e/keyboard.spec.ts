import { expect, test } from '@playwright/test';

// Keyboard-only play and screen-reader names on the main screens (spec terminal-ui accessibility
// requirements; task 1.1). Chromium only: WebKit's Tab skips buttons unless the OS setting is on.

test('regression: the mission buttons are named after their mission (were twelve "Iniciar missão")', async ({ page }) => {
	await page.goto('/?seed=4242');
	await expect(page.getByRole('button', { name: 'Iniciar missão: Missão 01 · Range' })).toBeVisible();
	await expect(page.getByRole('button', { name: 'Iniciar missão: Missão 04 · Calls e puts' })).toBeVisible();
});

test('regression: Enter on a focused tutorial button activates it (Skip and Previous were turned into Next)', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await expect(page.getByTestId('tut-step')).toHaveText('1 de 5');
	await page.keyboard.press('Enter'); // focus starts on Next
	await expect(page.getByTestId('tut-step')).toHaveText('2 de 5');
	await page.getByRole('button', { name: /Anterior/ }).focus();
	await page.keyboard.press('Enter');
	await expect(page.getByTestId('tut-step')).toHaveText('1 de 5');
	await page.getByRole('button', { name: 'Pular tutorial' }).focus();
	await page.keyboard.press('Enter');
	await expect(page.getByRole('button', { name: /Abrir terminal/ })).toBeVisible();
	await expect(page.getByTestId('tut-step')).toHaveCount(0);
});

test('regression: the calibration question takes the focus and keeps Tab inside (it was 35 Tabs away)', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	await expect(dialog).toBeVisible(); // asked at the first decision
	const slider = dialog.getByRole('slider');
	await expect(slider).toBeFocused();
	await expect(slider).toHaveAccessibleName(/Qual a probabilidade de o fechamento daqui a \d+ barras ficar ACIMA de/);
	await page.keyboard.press('ArrowRight');
	await expect(slider).toHaveAttribute('aria-valuetext', '55%');
	await page.keyboard.press('Tab');
	await expect(dialog.getByRole('button', { name: 'Pular' })).toBeFocused();
	await page.keyboard.press('Tab');
	await expect(dialog.getByRole('button', { name: 'Responder' })).toBeFocused();
	await page.keyboard.press('Tab'); // wraps instead of leaving the dialog
	await expect(slider).toBeFocused();
	await page.keyboard.press('Shift+Tab');
	await expect(dialog.getByRole('button', { name: 'Responder' })).toBeFocused();
	await page.keyboard.press('Enter');
	await expect(dialog).toBeHidden();
});
