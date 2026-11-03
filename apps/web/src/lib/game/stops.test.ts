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
