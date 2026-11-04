import { describe, expect, it } from 'vitest';
import { fmtNum, parseDecimal, priceToTick, qtyToLot, signMark } from './format.ts';

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
	it('regression: reads a level typed with a PT-BR decimal comma (was NaN)', () => {
		expect(parseDecimal('95,56')).toBe(95.56);
		expect(parseDecimal('95.56')).toBe(95.56);
		expect(parseDecimal(' 95,5 ')).toBe(95.5);
		expect(parseDecimal('1.234,5')).toBe(1234.5);
		expect(parseDecimal('1,234.5')).toBe(1234.5);
		expect(parseDecimal('-2,5')).toBe(-2.5);
		expect(parseDecimal('100')).toBe(100);
		expect(parseDecimal('')).toBeNaN();
		expect(parseDecimal('abc')).toBeNaN();
		expect(parseDecimal('9,5,1')).toBeNaN();
	});
	it('formats plain numbers in the locale', () => {
		expect(fmtNum(63, 'pt-BR', 1)).toBe('63,0');
		expect(fmtNum(0.75, 'en')).toBe('0.75');
		expect(fmtNum(-0.26, 'pt-BR', 2, true)).toBe('-0,26');
		expect(fmtNum(1.5, 'en', 2, true)).toBe('+1.50');
		expect(fmtNum(NaN, 'en')).toBe('—');
	});
	it('never relies on color alone', () => {
		expect([signMark(1), signMark(-1), signMark(0)]).toEqual(['▲', '▼', '•']);
	});
});
