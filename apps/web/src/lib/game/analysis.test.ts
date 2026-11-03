import { describe, expect, it } from 'vitest';
import type { DebriefData, EngineEvent, GameState } from '#lib/engine/types.ts';
import { analyze, type PlanEntry } from './analysis.ts';

function ev(p: Partial<EngineEvent>): EngineEvent {
	return {
		seq: 1, step: 0, type: 'order', orderId: 0, status: 'accepted', reason: 'none', side: 'buy',
		qty: '0', price: '0', requested: '0', amount: '0', pnl: '0', ambiguous: false, gap: false, capacityLimited: false,
		...p
	};
}

function game(events: EngineEvent[], extra: Partial<GameState> = {}): GameState {
	return {
		mission: 'm01-false-breakout', alias: 'K1A', status: 'finished', step: 10, lastDecisionStep: 9, warmupBars: 0,
		totalBars: 11, initialBalance: '10000', leverage: 1,
		contract: {} as GameState['contract'], bars: [], account: {} as GameState['account'],
		position: { qty: '0', entry: '0', margin: '0' }, orders: [], events,
		totals: { realizedPnl: '0', feesPaid: '0', fundingNet: '0', insuranceAbsorbed: '0', turnover: '0', peakEquity: '10000',
			maxDrawdown: '0', peakLeverageBp: 0, minLiqDistanceBp: -1, liquidations: 0, fills: 0, ambiguousFills: 0 },
		digest: '', eventsDigest: '', ...extra
	};
}

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
