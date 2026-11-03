import { describe, expect, it } from 'vitest';
import { CANDLE_PATTERNS, contextCandles } from './candlePatterns.ts';

describe('candle pattern reference', () => {
	it('has valid OHLC shapes and both locales for every pattern', () => {
		const ids = new Set<string>();
		for (const p of CANDLE_PATTERNS) {
			expect(ids.has(p.id)).toBe(false);
			ids.add(p.id);
			for (const [o, h, l, c] of p.candles) {
				expect(h).toBeGreaterThanOrEqual(Math.max(o, c));
				expect(l).toBeLessThanOrEqual(Math.min(o, c));
			}
			for (const loc of ['pt-BR', 'en'] as const) {
				const t = p.text[loc];
				expect(t.name && t.what && t.means && t.confirm).toBeTruthy();
			}
		}
	});

	it('draws a falling context before bullish reversals and a rising one before bearish ones', () => {
		const down = contextCandles('down', [40, 46, 8, 45]);
		expect(down).toHaveLength(3);
		expect(down[0][3]).toBeGreaterThan(down[2][3]);
		const up = contextCandles('up', [62, 92, 57, 58]);
		expect(up[0][3]).toBeLessThan(up[2][3]);
		expect(contextCandles(null, [1, 2, 0, 1])).toEqual([]);
	});

	it('engulfing bodies really engulf', () => {
		for (const id of ['bullish-engulfing', 'bearish-engulfing']) {
			const [a, b] = CANDLE_PATTERNS.find((p) => p.id === id)!.candles;
			expect(Math.max(b[0], b[3])).toBeGreaterThan(Math.max(a[0], a[3]));
			expect(Math.min(b[0], b[3])).toBeLessThan(Math.min(a[0], a[3]));
		}
	});
});
