import { describe, expect, it } from 'vitest';
import type { EngineFinding, EngineTrade } from '#lib/engine/types.ts';
import { describeFinding, describeLesson } from './analysis.ts';
import { fmtLeverageBp, fmtLiqDistance, greekRows, stopAdvice } from './debrief.ts';

const perp: EngineTrade = {
	n: 1,
	series: 0,
	side: 'long',
	entryStep: 61,
	exitStep: 64,
	qty: '10.00000000',
	entryAvg: '100.00000000',
	exitAvg: '95.96000000',
	pnl: '-50.00000000',
	fees: '0',
	funding: '0',
	net: '-50.00000000',
	bars: 3,
	reason: 'liquidation',
	plannedRisk: '50.00000000',
	rBp: -10000,
	planRespected: false,
	stop: '95.00000000',
	leverage: 20,
	liqPrice: '95.96000000',
	stopBeyondLiq: true,
	safeLeverage: 16
};

describe('greekRows', () => {
	it('lists delta, gamma, theta, vega and residual in order, as numbers', () => {
		const rows = greekRows({ delta: '12.5', gamma: '0.25', theta: '-3.10', vega: '-0.9', residual: '-0.34', total: '8.41' });
		expect(rows.map((r) => r.key)).toEqual(['delta', 'gamma', 'theta', 'vega', 'residual']);
		expect(rows.map((r) => r.value)).toEqual([12.5, 0.25, -3.1, -0.9, -0.34]);
	});
	it('is empty when no option was traded', () => {
		expect(greekRows(null)).toEqual([]);
	});
});

describe('debrief numbers follow the locale', () => {
	it('formats leverage from basis points', () => {
		expect(fmtLeverageBp(35000, 'pt-BR')).toBe('3,5x');
		expect(fmtLeverageBp(35000, 'en')).toBe('3.5x');
	});
	it('formats the closest liquidation distance, or a dash when never in a position', () => {
		expect(fmtLiqDistance(425, 'pt-BR')).toBe('4,25%');
		expect(fmtLiqDistance(425, 'en')).toBe('4.25%');
		expect(fmtLiqDistance(-1, 'en')).toBe('—');
	});
});

describe('stopAdvice', () => {
	it('gives the stop, the liquidation price and the leverage that keeps the stop in front', () => {
		expect(stopAdvice(perp, 'pt-BR')).toEqual({ stop: '95,00', liq: '95,96', lev: 16, used: 20 });
	});
	it('is null when the stop was in front of liquidation, and for option rows', () => {
		expect(stopAdvice({ ...perp, stopBeyondLiq: false, safeLeverage: null }, 'en')).toBeNull();
		expect(stopAdvice({ ...perp, series: 2 }, 'en')).toBeNull();
	});
});

describe('findings with the safe leverage', () => {
	const liq: EngineFinding = { code: 'liquidated', tone: 'bad', step: 64, count: 16, price: '95.96', requested: '0', amount: '50' };
	it('names the leverage that would have kept the stop in front', () => {
		expect(describeFinding(liq, 'pt-BR')).toContain('com até 16x de alavancagem');
		expect(describeFinding(liq, 'en')).toContain('up to 16x leverage');
	});
	it('falls back to the generic text without one', () => {
		expect(describeFinding({ ...liq, count: 0 }, 'en')).toContain('Lower leverage would have put the stop before liquidation');
	});
	it('describes a stop beyond liquidation with its prices', () => {
		const f: EngineFinding = { code: 'stop_beyond_liq', tone: 'bad', step: 61, count: 16, price: '95', requested: '95.96', amount: '0' };
		expect(describeFinding(f, 'pt-BR')).toContain('O stop em 95,00 (entrada na barra 61) ficou além da liquidação em 95,96');
		expect(describeFinding(f, 'en')).toContain('Up to 16x leverage');
	});
});

describe('theta and IV findings point to the Options lesson', () => {
	it('describes theta decay and an IV drop with the amount lost', () => {
		const theta: EngineFinding = { code: 'theta_decay', tone: 'warn', step: 0, count: 0, price: '0', requested: '0', amount: '1234.5' };
		expect(describeFinding(theta, 'pt-BR')).toContain('theta) custou 1.234,50 cr');
		expect(describeFinding({ ...theta, code: 'iv_crush' }, 'en')).toContain('(vega) cost 1,234.50 cr');
	});
	it('names the Options lesson', () => {
		expect(describeLesson('options', 'pt-BR')).toMatch(/^Opções II/);
		expect(describeLesson('options', 'en')).toMatch(/^Options II/);
	});
});
