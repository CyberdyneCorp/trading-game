import { describe, expect, it } from 'vitest';
import { EventNames } from './eventNames.ts';
import type { GameState } from '#lib/engine/types.ts';

type Seen = Pick<GameState, 'orders' | 'options'>;
const seen = (orders: Partial<GameState['orders'][number]>[], chain: Partial<GameState['options']['chain'][number]>[]): Seen =>
	({ orders, options: { chain, positions: [] } }) as unknown as Seen;

describe('EventNames', () => {
	it('keeps an order type after the order leaves the live list', () => {
		const n = new EventNames();
		n.observe(seen([{ id: 2, type: 'stop_market' }, { id: 3, type: 'take_profit' }], []));
		n.observe(seen([], []));
		expect(n.orderType(2)).toBe('STOP');
		expect(n.orderType(3)).toBe('TP');
		expect(n.orderType(9)).toBeUndefined();
	});

	it('names an option series after it expires and falls back to its id', () => {
		const n = new EventNames();
		n.observe(seen([], [{ id: 5, kind: 'call', strike: '100.00000000', expiryStep: 96 }]));
		n.observe(seen([], []));
		expect(n.seriesName(5)).toBe('C 100 · exp 96');
		expect(n.seriesName(7)).toBe('OPT#7');
	});
});
