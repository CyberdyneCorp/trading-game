// Heatmap and portfolio view models (add-practice-and-advanced-strategies W1-W7, W1-W8): the requests the
// panels send, how cells and warnings are shown; every number is the engine's (checked against the WASM
// build where it matters).
import { beforeAll, describe, expect, it } from 'vitest';
import type { GameState, WhatIfResult, WhatIfRiskView } from '#lib/engine/types.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import { game } from './fixtures.test-util.ts';
import { cellParams, cellStyle, ENGINE_MAX_POINTS, ENGINE_MAX_TIMES, heatmapRange, heatmapRequest, heatmapScale, heatmapTimes, HEATMAP_POINTS } from './heatmap.ts';
import { declaredLimits, deltaAfterHedge, exceededLimits, hedgeTicket, portfolioRows } from './portfolio.ts';
import { lastPrice } from './lastPrice.ts';

const risk = (p: Partial<WhatIfRiskView>): WhatIfRiskView => ({
	deltaUnits: '0', deltaNotional: '0', gamma1pct: '0', vegaPoint: '0', thetaBar: '0', fundingNext: '0', deltaBp: 0, gammaBp: 0,
	vegaBp: 0, thetaBp: 0, fundingBp: 0, equity: '10000', limits: [], ...p
});

describe('heatmap view model (design D4)', () => {
	it('times run from 0 to the last expiry in whole bars, at most 9 rows (engine cap 25)', () => {
		expect(heatmapTimes(40)).toEqual([0, 5, 10, 15, 20, 25, 30, 35, 40]);
		expect(heatmapTimes(3)).toEqual([0, 1, 2, 3]);
		expect(heatmapTimes(0)).toEqual([0]);
		expect(heatmapTimes(1000, 99).length).toBe(ENGINE_MAX_TIMES);
		expect(HEATMAP_POINTS).toBeLessThanOrEqual(ENGINE_MAX_POINTS);
	});

	it('prices: the forward ±15% on the tick; the request names the legs or the holdings', () => {
		expect(heatmapRange(101.37, 0.01)).toEqual({ lo: '86.16', hi: '116.58' });
		const r = heatmapRequest([{ series: 3, side: 'buy', qty: '1' }], 100, 0.01, 10);
		expect(r).toEqual({ legs: [{ series: 3, side: 'buy', qty: '1' }], heatmap: { lo: '85.00', hi: '115.00', points: 21, bars: [0, 1, 3, 4, 5, 6, 8, 9, 10] } });
		expect(heatmapRequest([], 100, 0.01, 4, true)).toMatchObject({ holdings: true, legs: [] });
	});

	it('cells: gain colour or loss colour (the colorblind setting swaps them) by |P&L| over the scale', () => {
		const m = { prices: ['90', '100'], times: [0, 5], pnl: [['-2.5', '1'], ['0', '5']] };
		expect(heatmapScale(m)).toBe(5);
		expect(cellStyle(5, 5)).toContain('var(--up) 95%');
		expect(cellStyle(-2.5, 5)).toContain('var(--down) 53%');
		expect(cellStyle(0, 5)).toContain('var(--up) 0%');
		expect(cellParams(m, 1, 1, 'pt-BR')).toEqual({ price: '100,00', bars: 5, pnl: '+5,00 cr' });
	});
});

describe('portfolio view model (design D9)', () => {
	const g = (p: Partial<GameState> = {}) => game([], { contract: { lotSize: '0.1', multiplier: '1' } as GameState['contract'], ...p });

	it('one row per held position, signed; warnings only for exceeded limits', () => {
		const r = { legs: [{ kind: 'perp', side: 'sell', qty: '3.00000000' }, { kind: 'call', side: 'buy', qty: '2.00000000' }] } as unknown as WhatIfResult;
		expect(portfolioRows(r).map((x) => x.qty)).toEqual([-3, 2]);
		const limits = [
			{ kind: 'delta' as const, limitBp: 1500, valueBp: 2000, exceeded: true },
			{ kind: 'theta' as const, limitBp: 5, valueBp: 1, exceeded: false }
		];
		expect(exceededLimits(risk({ limits })).map((l) => l.kind)).toEqual(['delta']);
		expect(exceededLimits(null)).toEqual([]);
	});

	it('the briefing lists the declared limits only', () => {
		expect(declaredLimits(g())).toEqual([]);
		const rules = { ...g().missionRules, riskLimits: { deltaBp: 1500, vegaBp: 150 } };
		expect(declaredLimits(g({ missionRules: rules }))).toEqual([
			{ kind: 'delta', bp: 1500 },
			{ kind: 'vega', bp: 150 }
		]);
	});

	it('hedge delta: -deltaUnits rounded toward zero to the lot, and the delta after it', () => {
		const h = hedgeTicket(g(), risk({ deltaUnits: '6.37000000' }))!;
		expect(h).toEqual({ series: 0, side: 'sell', qty: '6.3' });
		expect(deltaAfterHedge(g(), risk({ deltaUnits: '6.37000000' }), h)).toBeCloseTo(0.07, 10);
		expect(hedgeTicket(g(), risk({ deltaUnits: '0.04' }))).toBeNull();
		expect(hedgeTicket(g(), null)).toBeNull();
	});
});

describe.skipIf(!wasmBuilt)('heatmap and portfolio requests against the engine (WASM)', () => {
	let engine: NodeEngine;
	beforeAll(async () => {
		engine = await loadEngine();
	});

	it('the heatmap at time 0 equals the today curve at the same prices; holdings carry the risk block', () => {
		const run = engine.createRun('m16-range-condor', 4);
		const state = engine.state(run) as GameState;
		const q = state.options.chain.find((x) => x.listed)!;
		const legs = [{ series: q.id, side: 'buy' as const, qty: '1' }];
		const req = heatmapRequest(legs, lastPrice(state), Number(state.contract.tickSize), q.expiryStep - state.step);
		const grid = { lo: req.heatmap!.lo, hi: req.heatmap!.hi, points: req.heatmap!.points };
		const w = engine.runWhatif(run, { ...req, grid }) as WhatIfResult;
		expect(w.heatmap!.pnl[0]).toEqual(w.today.map(([, p]) => p));
		expect(w.heatmap!.times).toEqual(req.heatmap!.bars);
		const book = engine.runWhatif(run, { holdings: true, legs: [] }) as WhatIfResult;
		expect(book.legs).toEqual([]);
		expect(book.risk!.deltaUnits).toBe('0.00000000');
		expect(book.risk!.limits).toEqual([]);
		expect((engine.state(run) as GameState).digest).toBe(state.digest);
	});
});
