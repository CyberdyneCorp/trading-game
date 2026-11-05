import { describe, expect, it } from 'vitest';
import { fmtNum } from '#lib/game/format.ts';
import { candleCells, drawnCandles, hitsTag, LABEL_H, pinOf, priceRange, spreadLabels } from './chartScale.ts';

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

	it('regression: a stack overflowing the bottom does not push a distant top tag off the pane', () => {
		// Before: m01 seed 101, the LIQ and LIQ? tags pinned at the bottom slid every tag up by the
		// overflow, so the TP tag near the top was cut and its label drawn off-canvas.
		const ys = spreadLabels([12, 200, 396.5, 396.5], 0, 400);
		expect(ys[0]).toBe(12); // TP stays at its line
		expect(ys[1]).toBe(200);
		expect(ys[3]).toBe(400 - LABEL_H / 2);
		expect(ys[3] - ys[2]).toBeGreaterThanOrEqual(LABEL_H);
		expect(Math.min(...spreadLabels([4.5, 395, 396, 397], 0, 400))).toBeGreaterThanOrEqual(LABEL_H / 2);
	});

	it('shares the pane evenly when the tags cannot all fit, never leaving it', () => {
		const ys = spreadLabels([5, 6, 7, 8], 0, 40);
		expect(Math.min(...ys)).toBe(LABEL_H / 2);
		expect(Math.max(...ys)).toBe(40 - LABEL_H / 2);
		expect([...ys].sort((a, b) => a - b)).toEqual(ys); // order kept
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

describe('drawnCandles', () => {
	const bars = [
		[10, 12, 9, 11, 5],
		[11, 15, 10, 14, 7],
		[14, 14, 8, 9, 3],
		[9, 10, 7, 8, 9],
		[8, 9, 6, 7, 1]
	];

	it('draws every bar as is when a bar is at least a pixel wide', () => {
		expect(drawnCandles(bars, 4).map((k) => [k.at, k.o, k.h, k.l, k.c, k.v])).toEqual(bars.map((b, i) => [i, ...b]));
	});

	it('merges bars that would share a pixel column: first open, extremes, last close, largest volume', () => {
		// 0.5 px per bar: pairs share a column; the odd last bar stays alone.
		expect(drawnCandles(bars, 0.5)).toEqual([
			{ at: 0.5, o: 10, h: 15, l: 9, c: 14, v: 7 },
			{ at: 2.5, o: 14, h: 14, l: 7, c: 8, v: 9 },
			{ at: 4, o: 8, h: 9, l: 6, c: 7, v: 1 }
		]);
	});

	it('leaves out bars without trading (continuity bars), alone or inside a merged column', () => {
		const skip = [false, true, true, true, false];
		expect(drawnCandles(bars, 4, skip).map((k) => k.at)).toEqual([0, 4]);
		// 0.5 px per bar: [0, 1] keeps bar 0, [2, 3] is only continuity bars (no candle), [4] stays.
		expect(drawnCandles(bars, 0.5, skip)).toEqual([
			{ at: 0, o: 10, h: 12, l: 9, c: 11, v: 5 },
			{ at: 4, o: 8, h: 9, l: 6, c: 7, v: 1 }
		]);
	});

	it('draws 5,000 bars on a 1,368 px plot as one candle per column at most', () => {
		const many = Array.from({ length: 5000 }, (_, i) => [i, i + 2, i - 1, i + 1, i % 7]);
		const out = drawnCandles(many, 1368 / 5012);
		expect(out.length).toBeLessThanOrEqual(1368);
		expect(out[0]).toMatchObject({ o: 0, h: 5, l: -1, c: 4 });
	});
});
