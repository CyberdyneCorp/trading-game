import { describe, expect, it } from 'vitest';
import { ALL_CANDLE_PATTERNS } from './candlePatterns.ts';
import { FRAMES, OUTCOME_BARS, outcomes, STILL } from './patternOutcome.ts';

const hiOf = (cs: number[][]) => Math.max(...cs.map((c) => c[1]));
const loOf = (cs: number[][]) => Math.min(...cs.map((c) => c[2]));

describe('pattern outcomes (cheat sheet animation)', () => {
	it('every pattern gets two scenarios of valid, continuous candles', () => {
		for (const p of ALL_CANDLE_PATTERNS) {
			for (const o of outcomes(p)) {
				expect(o.candles).toHaveLength(OUTCOME_BARS);
				let open = p.candles[p.candles.length - 1][3];
				for (const [o2, h, l, c] of o.candles) {
					expect(o2).toBeCloseTo(open);
					expect(h).toBeGreaterThanOrEqual(Math.max(o2, c));
					expect(l).toBeLessThanOrEqual(Math.min(o2, c));
					open = c;
				}
			}
		}
	});

	it('textbook case: the confirmation candle closes beyond the pattern in its direction', () => {
		for (const p of ALL_CANDLE_PATTERNS.filter((x) => x.bias !== 'neutral')) {
			const [ok] = outcomes(p);
			const confirm = ok.candles[0][3];
			expect(ok.kind).toBe('confirmed');
			if (p.bias === 'bullish') expect(confirm).toBeGreaterThan(hiOf(p.candles));
			else expect(confirm).toBeLessThan(loOf(p.candles));
			expect(ok.level).toBe(p.bias === 'bullish' ? hiOf(p.candles) : loOf(p.candles));
		}
	});

	it('failure case: no confirmation, then a close through the stop on the other side of the pattern', () => {
		for (const p of ALL_CANDLE_PATTERNS.filter((x) => x.bias !== 'neutral')) {
			const [, bad] = outcomes(p);
			const bull = p.bias === 'bullish';
			const [first, second] = bad.candles;
			expect(bad.kind).toBe('failed');
			expect(bad.levelKind).toBe('stop');
			expect(bull ? first[3] < first[0] : first[3] > first[0]).toBe(true); // goes against the pattern
			expect(bull ? second[3] < loOf(p.candles) : second[3] > hiOf(p.candles)).toBe(true);
		}
	});

	it('neutral patterns break up, then down; the next candle decides', () => {
		for (const p of ALL_CANDLE_PATTERNS.filter((x) => x.bias === 'neutral')) {
			const [up, down] = outcomes(p);
			expect([up.kind, down.kind]).toEqual(['breakUp', 'breakDown']);
			expect(up.candles[0][3]).toBeGreaterThan(hiOf(p.candles));
			expect(down.candles[0][3]).toBeLessThan(loOf(p.candles));
		}
	});

	it('the loop shows the pattern alone, then each scenario candle by candle; the still frame is the full textbook case', () => {
		expect(FRAMES[0]).toEqual({ scenario: null, shown: 0 });
		expect(FRAMES.filter((f) => f.scenario === 0).map((f) => f.shown).slice(0, OUTCOME_BARS)).toEqual([1, 2, 3]);
		expect(FRAMES.filter((f) => f.scenario === 1).map((f) => f.shown).slice(0, OUTCOME_BARS)).toEqual([1, 2, 3]);
		expect(STILL).toEqual({ scenario: 0, shown: OUTCOME_BARS });
	});
});
