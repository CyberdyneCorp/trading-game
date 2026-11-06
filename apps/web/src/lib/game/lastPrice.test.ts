import { describe, expect, it } from 'vitest';
import { lastPrice } from './lastPrice.ts';
import type { BarTuple } from '#lib/engine/types.ts';

const bar = (close: string): BarTuple => ['1', '1', '1', close, '1', false];

describe('lastPrice', () => {
	// Regression: with a mark-price series the ticket priced orders at the mark (account.mark).
	it('is the close of the current bar, not the mark', () => {
		const s = { bars: [bar('96.10'), bar('97.70')], account: { mark: '97.45' } } as never;
		expect(lastPrice(s)).toBe(97.7);
	});

	it('falls back to the mark without bars', () => {
		expect(lastPrice({ bars: [], account: { mark: '50.5' } } as never)).toBe(50.5);
	});
});
