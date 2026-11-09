import { expect, test, type Page } from '@playwright/test';

// Pack catalog (web-client tasks 4.4, 5.3, 5.5) and Academy lessons (4.4).
// The service worker is blocked so page.route sees the pack requests (it never caches /catalog/*).
test.use({ serviceWorkers: 'block' });

const PACK = '**/catalog/packs/*.tlpack';

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

async function openCatalog(page: Page) {
	await page.getByTestId('open-catalog').click();
	await expect(page.getByRole('heading', { name: 'Catálogo de pacotes' })).toBeVisible();
	await expect(page.getByTestId('catalog-entry')).toHaveCount(3);
	await expect(page.getByTestId('catalog-tag').first()).toHaveText('SINTÉTICO');
}

const entryA = (page: Page) => page.getByTestId('catalog-entry').filter({ hasText: 'Mercado A · 1h' });

test('browse the catalog, interrupt a download, resume it after a reload, then play the pack', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await page.goto('/?seed=5');
	await openCatalog(page);
	const a = entryA(page);
	await expect(a).toContainText('4.740 barras'); // playtest: the count is grouped in PT-BR
	await expect(a).not.toContainText('4.800 barras'); // and the summary never disagrees with it
	await expect(a).toContainText('Alavancagem máx. 5x');
	await expect(a).toContainText('76 KB');

	// The connection drops on the second byte range: what arrived is kept.
	const ranges: string[] = [];
	await page.route(PACK, async (route) => {
		ranges.push(route.request().headers()['range'] ?? '');
		if (ranges.length === 2) return route.abort('internetdisconnected');
		return route.continue();
	});
	await a.getByTestId('catalog-download').click();
	await expect(a.getByTestId('download-state')).toContainText('Download interrompido em 64 KB de');
	await expect(a).toHaveAttribute('data-status', 'partial');
	expect(ranges).toEqual(['bytes=0-65535', 'bytes=65536-77311']);
	await page.unroute(PACK);

	// The partial download survives a reload and resumes with Range from byte 65536.
	await page.reload();
	await openCatalog(page);
	await expect(entryA(page).getByTestId('catalog-resume')).toContainText('Retomar download (64 KB de');
	const resumed: string[] = [];
	await page.route(PACK, (route) => (resumed.push(route.request().headers()['range'] ?? ''), route.continue()));
	await entryA(page).getByTestId('catalog-resume').click();
	await expect(entryA(page).getByTestId('catalog-installed')).toHaveText('INSTALADO');
	expect(resumed).toEqual(['bytes=65536-77311']);

	// Installed packs are practice markets: listed in the menu too, and playable.
	await entryA(page).getByTestId('catalog-play').click();
	// Playtest: a downloaded pack was briefed as "a market you imported". The samples are
	// publisher-signed synthetic packs, so the briefing says both where it came from and what it is.
	await expect(page.getByText(/Pacote sintético do catálogo — preços gerados/)).toBeVisible();
	await expect(page.getByRole('heading', { name: 'Treino · pacote do catálogo' })).toBeVisible();
	await expect(page.locator('main')).not.toContainText('você importou');
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('canvas').first()).toBeVisible();
	await answerIfAsked(page);
	// Regression: the synthetic sample packs were tagged CONHECIDO in the catalog and the top bar.
	await expect(page.getByTestId('market-tag')).toHaveText('SINTÉTICO');
	await expect(page.locator('header.top')).toContainText('Treino · pacote do catálogo');
	for (let i = 0; i < 3; i++) await page.keyboard.press('Space');
	await expect(page.locator('.turn')).toContainText('Barra 4');
	await page.getByRole('button', { name: /Menu/ }).first().click();
	await expect(page.getByTestId('pack-row')).toContainText('Mercado A · 1h');
	await expect(page.getByTestId('pack-row')).toContainText('4.740 barras');
	expect(errors).toEqual([]);
});

test('catalog offline and unavailable states; installed packs stay playable', async ({ page, context }) => {
	await page.goto('/?seed=5');
	await openCatalog(page);
	await page.getByTestId('catalog-entry').filter({ hasText: 'Mercado B · 4h' }).getByTestId('catalog-download').click();
	await expect(page.getByTestId('catalog-installed')).toHaveCount(1);
	await page.getByRole('button', { name: 'Voltar às missões' }).click();

	// Offline: the last catalog seen is listed, downloads need a connection, the installed pack plays.
	await context.setOffline(true);
	await openCatalog(page);
	await expect(page.getByTestId('catalog-state')).toContainText('O catálogo e os downloads precisam de conexão');
	await expect(page.getByText('Mostrando o último catálogo visto neste dispositivo.')).toBeVisible();
	await expect(entryA(page).getByTestId('catalog-needs-connection')).toHaveText('Precisa de conexão para baixar.');
	await expect(page.getByTestId('catalog-play')).toHaveCount(1);
	await page.getByRole('button', { name: 'Voltar às missões' }).click();
	await context.setOffline(false);

	// The server fails: unavailable, with a retry that recovers.
	let fail = true;
	await page.route('**/catalog/catalog.json', (route) => (fail ? route.fulfill({ status: 503, body: 'down' }) : route.continue()));
	await openCatalog(page);
	await expect(page.getByTestId('catalog-state')).toContainText('Catálogo indisponível agora');
	fail = false;
	await page.getByTestId('catalog-retry').click();
	await expect(page.getByTestId('catalog-state')).toHaveCount(0);
	await expect(entryA(page).getByTestId('catalog-download')).toBeVisible();
});

test('a corrupted download is refused before install and starts over', async ({ page }) => {
	await page.goto('/?seed=5');
	await openCatalog(page);
	await page.route(PACK, async (route) => {
		const res = await route.fetch();
		const body = Buffer.from(await res.body());
		body[100] ^= 0xff;
		await route.fulfill({ response: res, body });
	});
	await entryA(page).getByTestId('catalog-download').click();
	await expect(entryA(page).getByTestId('download-state')).toContainText('corrompido');
	await expect(entryA(page)).toHaveAttribute('data-status', 'none');
	await expect(page.getByTestId('catalog-installed')).toHaveCount(0);
});

// Market C is a schema-v2 sample pack built with the packer (scripts/build-sample-catalog.py): its
// own mission texts and objectives, and a story where a liquidity-outflow headline at the close of
// bar 15 is followed by a liquidity shock.
test('a v2 catalog pack briefs with its own texts and objectives, and its liquidity shock reaches the ticket', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	await page.goto('/?seed=5');
	await openCatalog(page);
	const c = page.getByTestId('catalog-entry').filter({ hasText: 'Mercado C · choque de liquidez' });
	await c.getByTestId('catalog-download').click();
	await expect(c.getByTestId('catalog-installed')).toHaveText('INSTALADO');
	await c.getByTestId('catalog-play').click();

	await expect(page.getByRole('heading', { name: 'Treino · choque de liquidez' })).toBeVisible();
	await expect(page.locator('main')).toContainText('Uma formadora de mercado vai sair do livro.');
	await expect(page.getByTestId('brief-shocks')).toBeVisible();
	await expect(page.getByTestId('brief-objective')).toHaveCount(3);
	await expect(page.getByTestId('brief-objective').nth(1)).toContainText('1%');
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('header.top')).toContainText('Treino · choque de liquidez');
	await answerIfAsked(page);
	await expect(page.getByTestId('ticket-shock')).toHaveCount(0);
	for (let i = 0; i < 14; i++) {
		await page.keyboard.press('Space');
		await answerIfAsked(page);
	}
	await expect(page.locator('.turn')).toContainText('Barra 15');
	await expect(page.getByTestId('ticket-shock')).toContainText('Choque de liquidez na próxima barra');

	// The menu's resume line names the run by the pack's own title.
	await page.getByRole('button', { name: /Menu/ }).first().click();
	await expect(page.getByTestId('pack-row').filter({ hasText: 'Mercado C' })).toBeVisible();
	await expect(page.locator('section.resume')).toContainText('Treino · choque de liquidez');
	expect(errors).toEqual([]);
});

test('Academy: the Foundations lesson runs from the curriculum map and passes its stage', async ({ page }) => {
	await page.goto('/');
	await page.getByTestId('open-curriculum').click();
	const foundations = page.locator('[data-stage=foundations]');
	await expect(foundations).toHaveAttribute('data-status', 'open');
	await expect(page.locator('[data-stage=structure]')).toHaveAttribute('data-status', 'open'); // the lesson never blocks
	await expect(page.getByTestId('curriculum-summary')).toHaveText('0 de 17 estágios com conteúdo aprovados');
	await foundations.getByTestId('lesson-start').click();

	await expect(page.getByTestId('lesson-phase')).toHaveText('CONCEITO');
	await expect(page.getByRole('heading', { name: 'Uma barra, quatro preços' })).toBeVisible();
	await expect(page.getByText('ILUSTRATIVO')).toBeVisible();
	await page.getByTestId('lesson-next').click();
	await page.getByTestId('lesson-next').click();
	await expect(page.getByTestId('lesson-phase')).toHaveText('DEMONSTRAÇÃO');
	await page.getByTestId('lesson-next').click();

	// Decision: a wrong answer explains and blocks Next; the right one unlocks it.
	await expect(page.getByTestId('lesson-phase')).toHaveText('DECISÃO');
	await expect(page.getByTestId('lesson-next')).toBeDisabled();
	await page.getByTestId('lesson-choice').filter({ hasText: 'Barra 4' }).click();
	await expect(page.getByTestId('lesson-feedback')).toContainText('Tente de novo.');
	await expect(page.getByTestId('lesson-next')).toBeDisabled();
	await page.getByTestId('lesson-choice').filter({ hasText: 'Barra 6' }).click();
	await expect(page.getByTestId('lesson-feedback')).toContainText('pavio inferior de 2,30');
	await page.getByTestId('lesson-next').click();

	await page.getByTestId('lesson-choice').filter({ hasText: '+1,21%' }).click(); // decimal comma in PT-BR
	await page.getByTestId('lesson-next').click();
	await expect(page.getByText('futuro', { exact: true })).toBeVisible(); // bars after 6 are the unknown future
	await expect(page.locator('table.sr-only tbody tr')).toHaveCount(6); // and are in no table or axis
	await page.getByTestId('lesson-choice').filter({ hasText: 'O fechamento da barra 6' }).click();
	await page.getByTestId('lesson-next').click();

	await expect(page.getByTestId('lesson-phase')).toHaveText('TRANSFERÊNCIA');
	await expect(page.locator('figcaption').getByText('Mercado Y · 4h')).toBeVisible();
	await page.getByTestId('lesson-choice').filter({ hasText: 'Barra 2' }).click();
	await page.getByTestId('lesson-next').click();
	await page.getByTestId('lesson-choice').filter({ hasText: '-1,28%' }).click();
	await page.getByTestId('lesson-next').click();

	await expect(page.getByTestId('lesson-score')).toHaveText('4 de 5 respostas certas na primeira tentativa');
	await page.getByTestId('lesson-finish').click();
	await expect(foundations).toHaveAttribute('data-status', 'passed');
	await expect(foundations.getByTestId('stage-lesson')).toContainText('concluída');
	await expect(page.getByTestId('curriculum-summary')).toHaveText('1 de 17 estágios com conteúdo aprovados');

	// Progress is local and survives a reload, even one right after the map shows the stage passed
	// (playtest: the write was not awaited and a quick reload lost it).
	await page.reload();
	await page.getByTestId('open-curriculum').click();
	await expect(page.locator('[data-stage=foundations]')).toHaveAttribute('data-status', 'passed');
});
