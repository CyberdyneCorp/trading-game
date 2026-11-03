import { describe, expect, it } from 'vitest';
import { buildTrades } from './trades.ts';
import { ev, game } from './fixtures.test-util.ts';
import type { PlanEntry } from './analysis.ts';

const plan = (step: number, risk: number, inv: number | null): PlanEntry => ({
	step, side: 'buy', thesis: 't', invalidation: inv, target: null, confidence: 60, plannedRisk: risk, skipped: false
});

describe('buildTrades', () => {
	it('splits flat-to-flat trades, computes R and exit reason (stopped long, my mission-01 game)', () => {
		const events = [
			ev({ seq: 1, step: 75, type: 'fill', side: 'buy', qty: '65.2', price: '103.24', requested: '103.2', amount: '3.37' }),
			ev({ seq: 2, step: 76, type: 'fill', side: 'sell', qty: '65.2', price: '101.76', requested: '101.8', amount: '3.32', pnl: '-96.5' }),
			ev({ seq: 3, step: 79, type: 'fill', side: 'sell', qty: '31.2', price: '100.02', amount: '1.56' }),
			ev({ seq: 4, step: 80, type: 'fill', side: 'buy', qty: '31.2', price: '97.2', requested: '97.2', amount: '0.61', pnl: '87.98' })
		];
		const t = buildTrades(game(events, { step: 139 }), [plan(60, 100, 101.8), { ...plan(78, 75, 102.2), side: 'sell' }]);
		expect(t).toHaveLength(2);
		expect(t[0]).toMatchObject({ side: 'long', exitReason: 'stop', bars: 1, planRespected: true });
		expect(t[0].r).toBeCloseTo(-1.032, 2);
		expect(t[1]).toMatchObject({ side: 'short', exitReason: 'target', bars: 1 });
		expect(t[1].r).toBeCloseTo((87.98 - 2.17) / 75, 3);
	});

	it('handles flips, manual closes, liquidation and open trades', () => {
		const events = [
			ev({ seq: 1, step: 1, type: 'fill', side: 'buy', qty: '2', price: '100' }),
			ev({ seq: 2, step: 2, type: 'fill', side: 'sell', qty: '5', price: '101', pnl: '2' }), // flip to short 3
			ev({ seq: 3, step: 4, type: 'liquidation', side: 'buy', qty: '3', price: '130', pnl: '-50' }),
			ev({ seq: 4, step: 6, type: 'fill', side: 'buy', qty: '1', price: '120' })
		];
		const t = buildTrades(game(events, { step: 9 }), []);
		expect(t.map((x) => x.exitReason)).toEqual(['manual', 'liquidation', 'open']);
		expect(t[1]).toMatchObject({ side: 'short', qty: 3, pnl: -50 });
		expect(t[2].exitStep).toBeNull();
		expect(t[2].r).toBeNull();
	});

	it('flags a plan that was not respected (exit far beyond invalidation)', () => {
		const events = [
			ev({ seq: 1, step: 1, type: 'fill', side: 'buy', qty: '1', price: '100' }),
			ev({ seq: 2, step: 5, type: 'fill', side: 'sell', qty: '1', price: '90', pnl: '-10' })
		];
		const t = buildTrades(game(events), [plan(0, 5, 95)]);
		expect(t[0].planRespected).toBe(false);
		expect(t[0].r).toBeCloseTo(-2, 6);
	});
});
