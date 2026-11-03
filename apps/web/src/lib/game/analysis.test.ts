import { describe, expect, it } from 'vitest';
import type { DebriefData } from '#lib/engine/types.ts';
import { ev, game } from './fixtures.test-util.ts';
import { analyze, type PlanEntry } from './analysis.ts';

const debrief = (equity: string): DebriefData => ({
	mission: 'm01', alias: 'K1A', status: 'finished', finalEquity: equity, buyAndHoldPnl: '0', flatPnl: '0',
	syntheticMarket: true, seed: 1, startPrice: '100', endPrice: '100'
});

const plan: PlanEntry = { step: 1, side: 'buy', thesis: 'breakout', invalidation: 95, target: 110, confidence: 60, plannedRisk: 100, skipped: false };

describe('analysis', () => {
	it('EDU02: not trading is not penalized', () => {
		const a = analyze(game([]), debrief('10000'), [], 0);
		expect(a.dims.risk.score).toBe(100);
		expect(a.dims.plan.score).toBeNull();
		expect(a.total).toBe(100);
		expect(a.findings.map((f) => f.code)).toContain('waited');
	});

	it('EDU01: a planned, stopped-out loss is credited as good process', () => {
		const events = [
			ev({ seq: 1, step: 1, type: 'fill', orderId: 1, side: 'buy', qty: '10', price: '100' }),
			ev({ seq: 2, step: 1, type: 'order', orderId: 2, status: 'accepted', side: 'sell', requested: '95' }),
			ev({ seq: 3, step: 4, type: 'fill', orderId: 2, side: 'sell', qty: '10', price: '94.9', pnl: '-51' })
		];
		const a = analyze(game(events), debrief('9949'), [plan], 0);
		expect(a.findings.map((f) => f.code)).toContain('good_process_loss');
		expect(a.dims.risk.score).toBe(100);
	});

	it('liquidation dominates the risk score and recommends Futures II', () => {
		const events = [
			ev({ seq: 1, step: 1, type: 'fill', orderId: 1, side: 'buy', qty: '10', price: '100' }),
			ev({ seq: 2, step: 3, type: 'liquidation', price: '91', amount: '100' })
		];
		const a = analyze(game(events, { status: 'finished' }), debrief('9900'), [plan], 1);
		expect(a.lesson).toBe('futures2');
		expect(a.dims.risk.score).toBeLessThanOrEqual(45);
	});

	it('profit without a plan is flagged as possibly luck', () => {
		const events = [ev({ seq: 1, step: 1, type: 'fill', orderId: 1, side: 'buy', qty: '10', price: '100' })];
		const a = analyze(game(events), debrief('10500'), [], 0);
		expect(a.findings.map((f) => f.code)).toEqual(expect.arrayContaining(['lucky_win', 'no_plan']));
	});
});
