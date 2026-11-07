import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Locator, type Page } from '@playwright/test';
import { recordWorkerMessages, requestKinds, workerLog } from './helpers.ts';

// What-if lab and strategy builder (add-academy-and-strategies tasks W5 and W7; specs options-whatif and
// strategy-builder): every number shown comes from the engine; combos are ordinary market orders sent
// longs first, withdrawn when a leg is refused, and flagged incomplete when a leg fails at the open.

const M05 = 4; // Missão 05 · Lançador (writing allowed)
const TEMPLATE_IDS = ['bull_call', 'bear_put', 'covered_call', 'protective_put', 'straddle', 'strangle', 'iron_condor'] as const;

async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

async function openBuilder(page: Page, seed = 515) {
	await page.goto(`/?seed=${seed}`);
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(M05).click();
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.getByTestId('tab-builder').click();
	await expect(page.getByTestId('strategy-builder')).toBeVisible();
}

async function expectNoViolations(page: Page, where: string, include?: string) {
	const axe = new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa']);
	const { violations } = await (include ? axe.include(include) : axe).analyze();
	expect(violations.map((v) => `${where}: ${v.id} ${v.nodes[0]?.target.join(' ')}`)).toEqual([]);
}

/** The payoff legend: lower scale end at the box's left edge, upper end at its right, the key between, nothing overlapping. */
async function expectLegendAtEdges(figure: Locator) {
	const box = (await figure.locator('svg').boundingBox())!;
	const spans = await figure.locator('figcaption > span').evaluateAll((els) => els.map((e) => e.getBoundingClientRect()).map((r) => ({ left: r.left, right: r.right })));
	expect(spans).toHaveLength(3);
	expect(Math.abs(spans[0].left - box.x)).toBeLessThan(2);
	expect(Math.abs(spans[2].right - (box.x + box.width))).toBeLessThan(2); // it sat mid-box at 1440 px
	for (let i = 1; i < spans.length; i++) expect(spans[i].left, 'legend items overlap').toBeGreaterThanOrEqual(spans[i - 1].right);
}

/** Moves a range input to `value` (keyboard-free: sets it and fires input). */
async function slide(page: Page, testid: string, value: number) {
	await page.getByTestId(testid).evaluate((el, v) => {
		const input = el as HTMLInputElement;
		input.value = String(v);
		input.dispatchEvent(new Event('input', { bubbles: true }));
	}, value);
}

test('Academy lab: an illustrative call priced by the engine; sliders change its numbers; at expiry no time value', async ({ page }) => {
	await page.goto('/?seed=4242');
	await page.getByTestId('open-lab').click();
	const lab = page.getByTestId('whatif-lab');
	await expect(lab.getByText(/Black-76 com taxa de juros zero/)).toBeVisible();
	await expect(lab.getByTestId('lab-pnl')).toHaveText(/0,00/); // entry = the model's price, no shift
	// The curve spans the whole chart box, so the scale ends printed at its edges are the curve's ends
	// (it was drawn in the middle third of a wide box).
	const [box, curve] = await Promise.all([lab.locator('figure svg').first().boundingBox(), lab.locator('figure path.today').first().boundingBox()]);
	expect(curve!.width).toBeGreaterThan(box!.width * 0.95);
	await expectLegendAtEdges(lab.locator('figure').first());
	const value = await lab.getByTestId('lab-value').innerText();
	await expect(lab.getByRole('textbox', { name: 'Preço de entrada' }).first()).toHaveValue(/^\d+,\d+$/); // the model's price, typed the PT-BR way
	await expect(lab.getByText('Valor da posição agora')).toBeVisible();
	await slide(page, 'lab-price', 10);
	await expect(lab.getByText('Valor da posição nesse cenário')).toBeVisible(); // a moved slider is a scenario, not "now"
	await expect(lab.getByTestId('lab-value')).not.toHaveText(value);
	await expect(lab.getByTestId('lab-pnl')).toHaveText(/^\+/);
	await slide(page, 'lab-iv', -30);
	await slide(page, 'lab-time', 30);
	await expect(lab.getByText(/No vencimento: cada opção vale só o valor intrínseco/)).toBeVisible();
	await expect(lab.getByTestId('whatif-value').first()).toHaveText('10,00'); // intrinsic at 110
	await lab.getByTestId('lab-reset').click();
	await lab.getByTestId('lab-add-option').click();
	await expect(lab.getByTestId('lab-leg')).toHaveCount(2);
	await expectNoViolations(page, 'academy lab');
});

test('terminal lab: at zero shift each leg equals the chain mid; the price slider moves the engine numbers', async ({ page }) => {
	await openBuilder(page);
	await page.getByTestId('template-straddle').click();
	await expect(page.getByTestId('builder-payoff')).toBeVisible();
	await page.getByTestId('builder-open-lab').click();
	const dialog = page.getByRole('dialog', { name: /Laboratório de opções/ });
	await expect(dialog.getByText(/nível de volatilidade implícita de hoje/)).toBeVisible();
	await expect(dialog.getByTestId('whatif-leg')).toHaveCount(2);
	const values = await dialog.getByTestId('whatif-value').allInnerTexts();
	const pnl0 = await dialog.getByTestId('lab-pnl').innerText();
	expect(values).toHaveLength(2);
	await slide(page, 'lab-price', 15);
	await expect(dialog.getByTestId('lab-pnl')).not.toHaveText(pnl0);
	await expectNoViolations(page, 'terminal lab', '[data-testid="whatif-lab"]');
	await dialog.getByTestId('lab-close').click();
	await expect(dialog).toBeHidden();
});

test('terminal lab agrees with the chain: zero shift = the engine quote mid, Greeks equal', async ({ page }) => {
	await recordWorkerMessages(page);
	await openBuilder(page);
	await page.getByTestId('template-bull_call').click();
	await page.getByTestId('builder-open-lab').click();
	const dialog = page.getByRole('dialog', { name: /Laboratório de opções/ });
	await expect(dialog.getByTestId('whatif-leg')).toHaveCount(2);
	const log = await workerLog(page);
	const state = log.fromWorker
		.map((m) => JSON.parse(m) as { res?: { options?: { chain: { id: number; mid: string; delta: string }[] } } })
		.findLast((r) => r.res?.options)?.res;
	const lab = log.fromWorker
		.map((m) => JSON.parse(m) as { res?: { ok?: boolean; value?: { legs: { series: number; value: string; delta: string }[] } } })
		.findLast((r) => r.res?.ok && r.res.value?.legs)?.res?.value;
	expect(state && lab).toBeTruthy();
	for (const leg of lab!.legs) {
		const q = state!.options!.chain.find((c) => c.id === leg.series)!;
		expect(Math.round(Number(leg.value) * 100)).toBe(Math.round(Number(q.mid) * 100));
		expect(leg.delta).toBe(q.delta);
	}
	expect(requestKinds(log)).toContain('runWhatif');
	expect(requestKinds(log)).toContain('comboPreview');
});

test('at 1024 px the builder never scrolls sideways: the Greeks table scrolls on its own, the payoff legend wraps', async ({ page }) => {
	await page.setViewportSize({ width: 1024, height: 768 });
	await openBuilder(page);
	await page.getByTestId('template-iron_condor').click();
	await expect(page.getByTestId('whatif-table')).toBeVisible();
	const builder = page.getByTestId('strategy-builder');
	const overflow = await builder.evaluate((el) => el.scrollWidth - el.clientWidth);
	expect(overflow).toBeLessThanOrEqual(1); // before: the 463 px table pushed the whole panel sideways
	const region = page.getByRole('region', { name: /Por perna/ });
	await expect(region).toHaveAttribute('tabindex', '0'); // a scrolling region keyboard users can reach
	await expectLegendAtEdges(page.getByTestId('builder-payoff'));
	// Each leg's chosen side and strike is readable, not clipped ("Vende", "90,0(" before).
	const clipped = await page.getByTestId('builder-leg').locator('select').evaluateAll((els) => {
		const ctx = document.createElement('canvas').getContext('2d')!;
		return els
			.map((el) => {
				const s = el as HTMLSelectElement;
				const cs = getComputedStyle(s);
				ctx.font = `${cs.fontSize} ${cs.fontFamily}`;
				const room = s.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight) - 20; // the native arrow
				return { text: s.selectedOptions[0]?.text ?? '', fits: ctx.measureText(s.selectedOptions[0]?.text ?? '').width <= room };
			})
			.filter((x) => !x.fits)
			.map((x) => x.text);
	});
	expect(clipped).toEqual([]);
	await expectNoViolations(page, 'builder at 1024', '[data-testid="strategy-builder"]');
});

test('every template (and the short versions) is built from the chain and analysed by the engine', async ({ page }) => {
	await openBuilder(page);
	for (const id of TEMPLATE_IDS) {
		await page.getByTestId(`template-${id}`).click();
		await expect(page.getByTestId('builder-leg')).toHaveCount(id === 'iron_condor' ? 4 : 2);
		await expect(page.getByTestId('builder-preview')).toBeVisible();
		await expect(page.getByTestId('builder-refused')).toHaveCount(0);
		await expect(page.getByTestId('builder-breakevens')).not.toHaveText('—');
		await expect(page.getByTestId('builder-max-loss')).not.toHaveText(/ilimitada/);
		await expect(page.getByTestId('builder-submit')).toBeEnabled();
	}
	await page.getByTestId('builder-short').check();
	for (const id of ['straddle', 'strangle'] as const) {
		await page.getByTestId(`template-${id}`).click();
		await expect(page.getByTestId('builder-max-loss')).toHaveText(/ilimitada acima de \d+,\d{2}/);
	}
	await expectNoViolations(page, 'builder', '[data-testid="strategy-builder"]');
});

test('writing locked: templates that sell options are disabled with the reason in a buy-only mission', async ({ page }) => {
	await page.goto('/?seed=4040');
	await page.getByRole('button', { name: /Iniciar missão/ }).nth(3).click(); // Missão 04 · options buyer
	await page.getByRole('button', { name: 'Pular tutorial' }).click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await answerIfAsked(page);
	await page.getByTestId('tab-builder').click();
	for (const id of ['bull_call', 'bear_put', 'covered_call', 'iron_condor']) await expect(page.getByTestId(`template-${id}`)).toBeDisabled();
	for (const id of ['protective_put', 'straddle', 'strangle']) await expect(page.getByTestId(`template-${id}`)).toBeEnabled();
	await expect(page.getByText(/lançar opções \(venda a descoberto\) é liberado no estágio Opções III/)).toBeVisible();
});

test('an iron condor previewed, submitted (longs first) and filled; the debrief groups its legs', async ({ page }) => {
	await recordWorkerMessages(page);
	await openBuilder(page, 616);
	await page.getByTestId('template-iron_condor').click();
	await expect(page.getByTestId('builder-preview').locator('li')).toHaveCount(4);
	await page.getByTestId('builder-submit').click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/Iron condor · 4 pernas: pernas enviadas/)).toBeVisible();
	const sides = (await workerLog(page)).toWorker
		.map((m) => (JSON.parse(m) as { req: { kind: string; side?: string } }).req)
		.filter((r) => r.kind === 'option')
		.map((r) => r.side);
	expect(sides).toEqual(['buy', 'buy', 'sell', 'sell']);
	await answerIfAsked(page);
	await page.keyboard.press('Space');
	await expect(page.getByText(/Execução #/).first()).toBeVisible();
	await expect(page.getByTestId('combo-incomplete')).toHaveCount(0);
	await page.keyboard.press('j'); // the run journal: one entry for the combo, with its state and premium
	await expect(page.getByTestId('journal-entry')).toHaveCount(1);
	await expect(page.getByTestId('journal-entry')).toContainText('Iron condor · 4 pernas');
	await expect(page.getByTestId('journal-combo')).toHaveText(/completo · prêmio líquido [+-]\d/);
	await page.keyboard.press('Escape');
	for (let i = 0; i < 80 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await answerIfAsked(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(40);
	}
	const combo = page.getByTestId('debrief-combo');
	await expect(combo).toHaveCount(1);
	await expect(combo).toContainText(/Iron condor · 4 pernas · completo ·\s+prêmio líquido [+-]\d/);
});

test('a refused leg cancels the legs already accepted (the dry run said yes; placement said no)', async ({ page }) => {
	// Fault injection: the dry run's reply is turned into "accepted", as if the state had changed since.
	await page.addInitScript(() => {
		const Base = window.Worker;
		const previews = new Set<number>();
		window.Worker = class extends Base {
			postMessage(message: unknown, transfer?: unknown) {
				const m = message as { id: number; req: { kind: string } };
				if (m.req?.kind === 'comboPreview') previews.add(m.id);
				return super.postMessage(message, transfer as Transferable[]);
			}
			set onmessage(fn: ((e: MessageEvent) => void) | null) {
				super.onmessage = fn
					? (e: MessageEvent) => {
							const d = e.data as { id: number; res?: { ok: boolean; value?: { accepted: boolean; legs: { accepted: boolean; reason: string }[] } } };
							if (previews.has(d.id) && d.res?.ok && d.res.value) {
								d.res.value.accepted = true;
								for (const l of d.res.value.legs) Object.assign(l, { accepted: true, reason: 'none' });
							}
							fn(e);
						}
					: null;
			}
		} as typeof Worker;
	});
	await recordWorkerMessages(page);
	await openBuilder(page, 717);
	await page.getByTestId('builder-qty').fill('1500');
	await page.getByTestId('template-iron_condor').click();
	await expect(page.getByTestId('builder-submit')).toBeEnabled();
	await page.getByTestId('builder-submit').click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/Perna 3 recusada: .*As pernas já aceitas foram canceladas/)).toBeVisible();
	const kinds = requestKinds(await workerLog(page)).filter((k) => k === 'option' || k === 'cancel');
	expect(kinds).toEqual(['option', 'option', 'option', 'cancel', 'cancel']);
});

test('a short leg with no bid is cancelled at the open: the combo is incomplete, its risk named, closed after confirmation', async ({ page }) => {
	await openBuilder(page, 818);
	await page.getByTestId('builder-add-leg').click(); // a long call near the money
	await page.getByTestId('builder-add-leg').click();
	const legs = page.getByTestId('builder-leg');
	const second = legs.nth(1);
	await second.getByLabel('Lado').selectOption('sell');
	const strikes = await second.getByLabel('Strike').locator('option').allInnerTexts();
	await second.getByLabel('Strike').selectOption({ label: strikes.at(-1)! }); // the farthest call: bid 0,00
	await expect(page.getByTestId('builder-no-bid')).toBeVisible();
	await page.getByTestId('builder-submit').click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await answerIfAsked(page);
	await page.keyboard.press('Space');
	const notice = page.getByTestId('combo-incomplete');
	await expect(notice).toBeVisible();
	await expect(notice).toContainText(/Combo incompleto: Pernas livres/);
	await expect(notice).toContainText(/Pernas abertas: compra C/);
	await expect(notice.getByTestId('combo-risk')).toHaveText(/Risco agora: perda máxima de/);
	await notice.getByTestId('combo-close').click();
	await page.getByTestId('confirm-yes').click();
	await expect(page.getByText(/Ordens para fechar as pernas abertas enviadas/)).toBeVisible();
	await expect(notice).toBeHidden();
});
