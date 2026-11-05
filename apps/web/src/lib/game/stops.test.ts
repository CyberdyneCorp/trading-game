import { describe, expect, it } from 'vitest';
import {
	distanceOf,
	rewardRisk,
	stopFromAtr,
	stopFromPct,
	stopOnLosingSide,
	swingStop,
	targetFromR,
	targetOnWinningSide,
	trailRateFromPercent,
	trailTriggerFromPercent,
	type BarTuple
} from './stops.ts';

const bars: BarTuple[] = [
	[100, 102, 98, 101, 1],
	[101, 104, 99, 103, 1],
	[103, 105, 100, 104, 1]
];

describe('stop presets', () => {
	it('places ATR stops on the losing side', () => {
		expect(stopFromAtr('buy', 100, 2, 1.5)).toBe(97);
		expect(stopFromAtr('sell', 100, 2, 1.5)).toBe(103);
		expect(stopFromAtr('buy', 100, null, 1)).toBeNull(); // ATR still warming up
	});

	it('places percent stops on the losing side', () => {
		expect(stopFromPct('buy', 200, 2)).toBeCloseTo(196);
		expect(stopFromPct('sell', 200, 2)).toBeCloseTo(204);
	});

	it('puts swing stops one tick beyond the recent extreme', () => {
		expect(swingStop('buy', 104, bars, 0.01)).toBeCloseTo(97.99);
		expect(swingStop('sell', 104, bars, 0.01)).toBeCloseTo(105.01);
		expect(swingStop('buy', 97, bars, 0.01)).toBeNull(); // entry already below the swing low
		expect(swingStop('buy', 104, bars, 0.01, 1)).toBeCloseTo(99.99); // lookback respected
	});
});

describe('targets and ratios', () => {
	it('derives targets from R multiples', () => {
		expect(targetFromR('buy', 100, 95, 2)).toBe(110);
		expect(targetFromR('sell', 100, 105, 3)).toBe(85);
		expect(targetFromR('buy', 100, null, 2)).toBeNull();
	});

	it('reports distance in price, percent and ATR', () => {
		expect(distanceOf(100, 97, 2)).toEqual({ abs: 3, pct: 3, atr: 1.5 });
		expect(distanceOf(100, 97, null)?.atr).toBeNull();
	});

	it('computes reward/risk only when both levels are on their sides', () => {
		expect(rewardRisk('buy', 100, 95, 110)).toBe(2);
		expect(rewardRisk('sell', 100, 105, 90)).toBe(2);
		expect(rewardRisk('buy', 100, 105, 110)).toBeNull();
	});

	it('validates sides', () => {
		expect(stopOnLosingSide('buy', 100, 99)).toBe(true);
		expect(stopOnLosingSide('sell', 100, 99)).toBe(false);
		expect(targetOnWinningSide('sell', 100, 99)).toBe(true);
	});
});

describe('percentage trailing stop (policy v16)', () => {
	it('turns a typed percent into the engine rate string', () => {
		expect(trailRateFromPercent(2)).toBe('0.020000');
		expect(trailRateFromPercent(2.5)).toBe('0.025000');
		expect(trailRateFromPercent(0.0125)).toBe('0.000125');
		expect(trailRateFromPercent(0)).toBeNull();
		expect(trailRateFromPercent(100)).toBeNull();
		expect(trailRateFromPercent(Number.NaN)).toBeNull();
	});

	it('estimates the first trigger on the exit side of the close', () => {
		expect(trailTriggerFromPercent('sell', 100, 2, 0.01)).toBeCloseTo(98, 9);
		expect(trailTriggerFromPercent('buy', 100, 2, 0.01)).toBeCloseTo(102, 9);
		expect(trailTriggerFromPercent('sell', 100, 0, 0.01)).toBeNull();
	});

	it('regression: rounds outward onto the tick like the engine (showed 94,75, engine set 94,74)', () => {
		// m07 seed 707: close 97.68, 3% -> 94.7496, the engine floors a sell trail to 94.74.
		expect(trailTriggerFromPercent('sell', 97.68, 3, 0.01)).toBeCloseTo(94.74, 9);
		// 100.37 + 1.5% = 101.87555 -> 101.88 (the engine test's value).
		expect(trailTriggerFromPercent('buy', 100.37, 1.5, 0.01)).toBeCloseTo(101.88, 9);
	});
});
