import { describe, expect, it } from 'vitest';
import { breakeven, maxLoss, payoffAt, writeMargin, type OptionLeg } from './optionsMath.ts';
import type { OptionQuoteView } from '#lib/engine/types.ts';

const call: OptionLeg = { kind: 'call', strike: 100, side: 'buy', qty: 2, premium: 3, multiplier: 1 };
const put: OptionLeg = { kind: 'put', strike: 100, side: 'buy', qty: 1, premium: 4, multiplier: 1 };

describe('optionsMath', () => {
	it('long call/put payoff at expiry and breakeven', () => {
		expect(payoffAt(call, 90)).toBe(-6); // loses the premium, never more
		expect(payoffAt(call, 110)).toBe(14);
		expect(breakeven(call)).toBe(103);
		expect(payoffAt(put, 92)).toBe(4);
		expect(breakeven(put)).toBe(96);
		expect(maxLoss(call)).toBe(6);
	});

	it('short payoff mirrors the long and has no bounded max loss', () => {
		const short = { ...call, side: 'sell' as const };
		expect(payoffAt(short, 110)).toBe(-14);
		expect(payoffAt(short, 90)).toBe(6);
		expect(maxLoss(short)).toBeNull();
	});

	it('write margin uses the engine formula', () => {
		const q = { kind: 'call', strike: '110', ask: '1.50' } as OptionQuoteView;
		// F = 100: OTM = 10 -> max(15 - 10, 10) + 1.5 = 11.5 per unit
		expect(writeMargin(q, 100, 2, 1)).toBeCloseTo(23, 6);
	});
});
