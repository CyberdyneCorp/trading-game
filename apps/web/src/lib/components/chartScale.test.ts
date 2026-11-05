import { describe, expect, it } from 'vitest';
import { fmtNum } from '#lib/game/format.ts';
import { candleCells, hitsTag, LABEL_H, pinOf, priceRange, spreadLabels } from './chartScale.ts';

describe('candleCells', () => {
	it('regression: the screen-reader candle table uses the locale, not raw engine decimals', () => {
		expect(candleCells([1234.5, 1240, 1230.25, 1235, 98765.4321], 'pt-BR')).toEqual(['1.234,50', '1.240,00', '1.230,25', '1.235,00', '98.765']);
		expect(candleCells([99.1, 99.2, 99, 99.15, 12], 'en')).toEqual(['99.10', '99.20', '99.00', '99.15', '12']);
	});
});

describe('fmtNum', () => {
	it('regression: a value that rounds to zero never shows as "-0,00" (a far put delta in the chain)', () => {
		expect(fmtNum(-0.0004, 'pt-BR')).toBe('0,00');
		expect(fmtNum(-0.25, 'pt-BR')).toBe('-0,25');
	});
});

describe('priceRange', () => {
	it('regression: a far liquidation level does not squash the candles', () => {
		// Before: any level within ±20% widened the scale (96.7–97.6 candles + LIQ 78.3).
		const [lo, hi] = priceRange(96.7, 97.6, [97.4, 95.76, 78.33]);
		expect(lo).toBeGreaterThan(95);
		expect(hi).toBeLessThan(99);
		expect(pinOf(78.33, lo, hi)).toBe('below');
	});

	it('includes levels close to the candles', () => {
		const [lo, hi] = priceRange(100, 104, [99, 105.5]);
		expect(lo).toBeLessThan(99);
		expect(hi).toBeGreaterThan(105.5);
		expect(pinOf(99, lo, hi)).toBeNull();
	});

	it('pins levels above the scale too', () => {
		const [lo, hi] = priceRange(100, 101, [140]);
		expect(pinOf(140, lo, hi)).toBe('above');
	});
});

describe('spreadLabels', () => {
	it('regression: tags at the same price no longer overlap (ENTRY? on ENTRY)', () => {
		const ys = spreadLabels([100, 101, 300], 0, 400);
		expect(ys[1] - ys[0]).toBeGreaterThanOrEqual(LABEL_H);
		expect(ys[2]).toBe(300);
	});

	it('keeps tags inside the pane, sliding a stack up from the bottom', () => {
		const ys = spreadLabels([395, 396, 397], 0, 400);
		expect(Math.max(...ys)).toBeLessThanOrEqual(400 - LABEL_H / 2);
		const sorted = [...ys].sort((a, b) => a - b);
		expect(sorted[1] - sorted[0]).toBeGreaterThanOrEqual(LABEL_H);
		expect(sorted[2] - sorted[1]).toBeGreaterThanOrEqual(LABEL_H);
	});
});

describe('hitsTag', () => {
	it('regression: an axis price under the ENTRY? tag is hidden, one clear of it is drawn', () => {
		const tags = spreadLabels([212], 0, 400); // ENTRY? 98,16
		expect(hitsTag(205, tags)).toBe(true); // the 98,46 grid label half-hidden behind it
		expect(hitsTag(212 + LABEL_H, tags)).toBe(false);
		expect(hitsTag(205, [])).toBe(false);
	});
});
