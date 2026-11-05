import { expect, test } from '@playwright/test';

// Supported browsers (spec web-client "Supported browsers"; task 1.3).

test('a browser without WebAssembly gets the unsupported-browser message, not a broken game', async ({ page }) => {
	await page.addInitScript(() => {
		delete (globalThis as { WebAssembly?: unknown }).WebAssembly;
	});
	await page.goto('/');
	const msg = page.getByTestId('unsupported');
	await expect(msg).toBeVisible();
	await expect(msg).toContainText('Navegador não suportado');
	await expect(msg).toContainText('WebAssembly');
	await expect(msg).not.toContainText('Ed25519');
	await expect(page.getByRole('button', { name: /Iniciar missão/ })).toHaveCount(0);
});

test('a browser without Ed25519 in WebCrypto gets the unsupported-browser message', async ({ page }) => {
	await page.addInitScript(() => {
		const importKey = crypto.subtle.importKey.bind(crypto.subtle);
		crypto.subtle.importKey = ((format: never, key: never, alg: AlgorithmIdentifier, ...rest: never[]) =>
			(typeof alg === 'object' ? alg.name : alg) === 'Ed25519'
				? Promise.reject(new DOMException('Unrecognized name', 'NotSupportedError'))
				: importKey(format, key, alg, ...(rest as [never, never]))) as typeof crypto.subtle.importKey;
	});
	await page.goto('/');
	const msg = page.getByTestId('unsupported');
	await expect(msg).toContainText('Assinaturas Ed25519 no WebCrypto');
	await expect(msg).toContainText('Chrome, Edge, Firefox ou Safari');
	await expect(page.getByRole('button', { name: /Iniciar missão/ })).toHaveCount(0);
});

test('a supported browser goes straight to the menu', async ({ page }) => {
	await page.goto('/');
	await expect(page.getByRole('button', { name: /Iniciar missão/ }).first()).toBeVisible();
	await expect(page.getByTestId('unsupported')).toHaveCount(0);
});
