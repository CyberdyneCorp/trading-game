import { describe, expect, it } from 'vitest';
import type { WhatIfResult } from '#lib/engine/types.ts';
import {
	academyMaxDays,
	academyRequest,
	chartGeometry,
	clampShift,
	runMaxBars,
	runRequest,
	setSlider,
	strikesOf,
	unboundedAbove,
	type AcademyLeg
} from './whatifLab.ts';

const CALL: AcademyLeg = { kind: 'call', side: 'buy', strike: 100, qty: 1, days: 30, ivPct: 60, entry: 6.85 };

describe('what-if lab requests', () => {
	it('Academy: one step a day, IV as a fraction, the shift as forward, minutes and vol points', () => {
		const r = academyRequest([CALL, { ...CALL, kind: 'perp', entry: 100 }], { price: 5, time: 2, iv: -5 });
		expect(r).toEqual({
			forward: '100.00',
			minutesPerStep: 1440,
			multiplier: '1',
			legs: [
				{ kind: 'call', side: 'buy', qty: '1', strike: '100.00', minutesToExpiry: 43200, iv: '0.6000', entry: '6.85' },
				{ kind: 'perp', side: 'buy', qty: '1', entry: '100.00' }
			],
			shift: { forward: '105.00', minutesElapsed: 2880, ivPoints: '-5' },
			grid: { lo: '70.00', hi: '130.00', points: 61 }
		});
	});

	it('terminal: the run legs as given, time in bars', () => {
		const r = runRequest([{ series: 3, side: 'sell', qty: '2' }], { price: -10, time: 4, iv: 3 }, 50);
		expect(r).toEqual({
			legs: [{ series: 3, side: 'sell', qty: '2' }],
			shift: { forward: '45.00', bars: 4, ivPoints: '3' },
			grid: { lo: '35.00', hi: '65.00', points: 61 }
		});
	});

	it('the time range ends at the nearest expiry; shifts are clamped; locked sliders do not move', () => {
		expect(academyMaxDays([CALL, { ...CALL, days: 10 }, { ...CALL, kind: 'perp', days: 0 }])).toBe(10);
		expect(runMaxBars([90, 80], 70)).toBe(10);
		expect(runMaxBars([], 70)).toBe(0);
		expect(clampShift({ price: 99, time: 50, iv: -99 }, 10)).toEqual({ price: 30, time: 10, iv: -30 });
		const s = { price: 0, time: 0, iv: 0 };
		expect(setSlider(s, 'price', 5, ['iv'])).toBe(s);
		expect(setSlider(s, 'iv', -15, ['iv'])).toEqual({ price: 0, time: 0, iv: -15 });
	});
});

describe('payoff chart from engine curves', () => {
	const result = {
		forward: '100',
		atExpiry: false,
		legs: [{ strike: '105' }, { strike: '95' }, { strike: null }],
		net: {},
		expiry: { breakevens: ['91', '109'], maxProfit: '4', maxLoss: null, curve: [['80', '-7'], ['100', '4'], ['120', '-7']] },
		today: [['80', '-5'], ['100', '2'], ['120', '-5']]
	} as unknown as WhatIfResult;

	it('maps prices to x and P&L to y around the zero line; breakevens, strikes and the forward are marked', () => {
		const g = chartGeometry(result, 200, 100, strikesOf(result))!;
		expect(g.zeroY).toBe(50);
		expect(g.forwardX).toBe(100);
		expect(g.breakevenX.map((x) => Math.round(x))).toEqual([55, 145]);
		expect(g.strikeX).toEqual([75, 125]);
		expect(g.expiryPath?.startsWith('M0.0,')).toBe(true);
		expect(g.todayPath.split('L')).toHaveLength(3);
		expect(strikesOf(result)).toEqual([95, 105]);
	});

	it('an unbounded loss is named above the upper breakeven; none without expiry analytics', () => {
		expect(unboundedAbove(result)).toBe('109');
		expect(unboundedAbove({ ...result, expiry: { ...result.expiry!, maxLoss: '3' } })).toBeNull();
		expect(unboundedAbove({ ...result, expiry: null })).toBeNull();
		expect(chartGeometry({ ...result, today: [] }, 200, 100)).toBeNull();
	});
});
