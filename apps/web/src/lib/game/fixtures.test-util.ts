import type { BarTuple, EngineEvent, GameState } from '#lib/engine/types.ts';

export function ev(p: Partial<EngineEvent>): EngineEvent {
	return {
		seq: 1, step: 0, type: 'order', orderId: 0, status: 'accepted', reason: 'none', side: 'buy',
		qty: '0', price: '0', requested: '0', amount: '0', pnl: '0', ambiguous: false, gap: false, capacityLimited: false,
		...p
	};
}

export function bars(closes: number[]): BarTuple[] {
	return closes.map((c) => [String(c), String(c), String(c), String(c), '1', true]);
}

export function game(events: EngineEvent[], extra: Partial<GameState> = {}): GameState {
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
