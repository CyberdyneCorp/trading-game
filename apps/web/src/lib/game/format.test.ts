import { describe, expect, it } from 'vitest';
import { fmtNum, inputText, parseAmount, parseDecimal, planHeadline, priceInputText, priceToTick, qtyInputText, qtyToLot, signMark } from './format.ts';

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
	it('regression: a level written into a PT-BR input uses the decimal comma (chips wrote "99.96")', () => {
		expect(priceInputText(99.957, 0.01, 'pt-BR')).toBe('99,96');
		expect(priceInputText(1234.5, 0.01, 'pt-BR')).toBe('1234,50'); // no grouping in an input
		expect(priceInputText(99.957, 0.01, 'en')).toBe('99.96');
		expect(priceInputText(-1, 0.01, 'pt-BR')).toBe('');
		expect(parseDecimal(priceInputText(106.594, 0.01, 'pt-BR'))).toBe(106.59); // reads back
	});
	it('regression: a quantity written into a PT-BR input uses the decimal comma (advisor apply wrote "16.6")', () => {
		expect(qtyInputText(16.66, 0.1, 'pt-BR')).toBe('16,6');
		expect(qtyInputText(16.66, 0.1, 'en')).toBe('16.6');
		expect(qtyInputText(3, 1, 'pt-BR')).toBe('3');
		expect(parseDecimal(qtyInputText(120.44, 0.1, 'pt-BR'))).toBe(120.4); // reads back
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
	it('regression: a credit amount typed with a thousands separator is read as thousands ("1.000" was 1)', () => {
		expect(parseAmount('1.000', 'pt-BR')).toBe(1000);
		expect(parseAmount('12.500.000', 'pt-BR')).toBe(12_500_000);
		expect(parseAmount('1.000,50', 'pt-BR')).toBe(1000.5);
		expect(parseAmount('100,5', 'pt-BR')).toBe(100.5);
		expect(parseAmount('100.5', 'pt-BR')).toBe(100.5); // not a thousands group: still a decimal
		expect(parseAmount('1,000', 'en')).toBe(1000);
		expect(parseAmount('1.000', 'en')).toBe(1);
		expect(parseAmount('2,5', 'en')).toBe(2.5);
		expect(parseAmount('', 'pt-BR')).toBeNaN();
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

describe('planHeadline', () => {
	it('regression: the plan dialog quantity uses the locale (49,9 in PT-BR, not 49.9)', () => {
		expect(planHeadline({ side: 'buy', qty: '49.9', leverage: 3 }, null, 'pt-BR')).toBe('▲ LONG 49,9 · 3x');
		expect(planHeadline({ side: 'sell', qty: '1500', leverage: 1 }, null, 'pt-BR')).toBe('▼ SHORT 1.500,0 · 1x');
		expect(planHeadline({ side: 'buy', qty: '49.9', leverage: 3 }, null, 'en')).toBe('▲ LONG 49.9 · 3x');
	});

	it('shows whole option contracts without decimals', () => {
		expect(planHeadline({ side: 'sell', qty: '20', leverage: 1 }, 'P 100 · exp 96', 'pt-BR')).toBe('▼ P 100 · exp 96 × 20');
	});
});

describe('inputText', () => {
	it('writes a number the way the locale types it (the lab filled 6.85 on a PT-BR screen)', () => {
		expect(inputText(6.85, 'pt-BR')).toBe('6,85');
		expect(inputText(6.85, 'en')).toBe('6.85');
		expect(inputText(100, 'pt-BR')).toBe('100');
	});
});
