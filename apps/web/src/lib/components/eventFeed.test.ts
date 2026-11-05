import { describe, expect, it } from 'vitest';
import { describeEvent } from './EventFeed.svelte';
import { EventNames } from '#lib/game/eventNames.ts';
import type { EngineEvent } from '#lib/engine/types.ts';

const fill = (over: Partial<EngineEvent>): EngineEvent =>
	({
		seq: 1, step: 100, type: 'fill', orderId: 3, orderType: 'market', status: 'accepted', reason: 'none', side: 'buy', qty: '200.00000000',
		price: '24.47000000', requested: '0.00000000', amount: '5.88000000', pnl: '-4474.00000000', ambiguous: false,
		gap: false, capacityLimited: false, series: 4, approximate: false, ...over
	}) as EngineEvent;

describe('describeEvent', () => {
	it('regression: a margin-call buy-back reads as a forced buy-back, not "Execução #0"', () => {
		const names = new EventNames();
		names.rememberSeries({ id: 4, kind: 'put', strike: '100.00000000', expiryStep: 108 });
		const text = describeEvent(fill({ orderId: 0 }), 'pt-BR', names)?.text ?? '';
		expect(text).toMatch(/^P 100 · exp 108 Recompra forçada \(chamada de margem\): BUY 200,0 @ 24,47/);
		expect(text).not.toMatch(/#0/);
	});

	it('regression: a bracket leg never seen in the live order list is named by its event type', () => {
		// Before: legs placed and filled or cancelled in one advance read "LMT/STOP/TP".
		const leg = fill({ type: 'order', status: 'accepted', orderId: 6, orderType: 'stop_market', side: 'sell', series: 0, qty: '52.80000000', requested: '99.30000000' });
		expect(describeEvent(leg, 'pt-BR')?.text).toMatch(/Ordem #6 aceita \(STOP venda 52,8\)/);
		expect(describeEvent({ ...leg, orderType: 'take_profit', side: 'buy' }, 'pt-BR')?.text).toMatch(/\(TP compra 52,8\)/);
	});

	it('a moved stop reads as moved to its new price, not as a new order', () => {
		const moved = fill({ type: 'order', status: 'accepted', reason: 'order_moved', orderId: 6, orderType: 'stop_market', side: 'sell', series: 0, requested: '97.50000000' });
		expect(describeEvent(moved, 'pt-BR')?.text).toBe('Ordem #6 movida para 97,50');
	});

	it('names an ordinary fill by its order id', () => {
		expect(describeEvent(fill({}), 'pt-BR')?.text).toMatch(/Execução #3/);
	});
});
