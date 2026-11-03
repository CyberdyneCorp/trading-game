import { describe, expect, it } from 'vitest';
import { priceToTick, qtyToLot, signMark } from './format.ts';

describe('format', () => {
	it('floors quantities to the lot and emits engine decimal strings', () => {
		expect(qtyToLot(16.66, 0.1)).toBe('16.6');
		expect(qtyToLot(0.05, 0.1)).toBe('0');
		expect(qtyToLot(3, 1)).toBe('3');
	});
	it('rounds prices to the tick', () => {
		expect(priceToTick(100.123, 0.01)).toBe('100.12');
		expect(priceToTick(-1, 0.01)).toBe('');
	});
	it('never relies on color alone', () => {
		expect([signMark(1), signMark(-1), signMark(0)]).toEqual(['▲', '▼', '•']);
	});
});
