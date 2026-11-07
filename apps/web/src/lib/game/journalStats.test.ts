import { describe, expect, it } from 'vitest';
import type { DebriefData, EngineTrade } from '#lib/engine/types.ts';
import { coreStats, journalStats, missingSummaries, mistakes, PRACTICE_STAGE, rBucket, statsRuns, trend, type StatsRun } from './journalStats.ts';
import { summarize, type RunSummary, type TradeSummary } from './runSummary.ts';

const trade = (p: Partial<TradeSummary> = {}): TradeSummary => ({ net: 0, rBp: null, riskPct: null, reason: 'manual', respected: null, movedAway: null, option: false, ...p });

function run(id: string, at: number, trades: TradeSummary[], p: Partial<RunSummary> & { missionId?: string } = {}): StatsRun {
	const missionId = p.missionId ?? 'm01-false-breakout';
	return { runId: id, missionId, createdAt: at, summary: { v: 1, mission: missionId, status: 'finished', initial: 10000, net: 0, score: 70, trades, findings: [], ...p } };
}

describe('journal statistics (design D7)', () => {
	it('win rate and expectancy: R of +2, -1, -1 and +0.5 is 50% and +0.125R (spec scenario)', () => {
		const c = coreStats([
			run('a', 1, [trade({ net: 200, rBp: 20000 }), trade({ net: -100, rBp: -10000 })]),
			run('b', 2, [trade({ net: -100, rBp: -10000 }), trade({ net: 50, rBp: 5000 })])
		]);
		expect(c.trades).toBe(4);
		expect(c.winRate).toBe(0.5);
		expect(c.expectancyR).toBeCloseTo(0.125, 10);
		expect(c.avgWinR).toBeCloseTo(1.25, 10);
		expect(c.avgLossR).toBe(-1);
		expect(c.rBuckets).toEqual([0, 2, 0, 1, 1, 0]);
	});

	it('open trades have no R and do not count as closed; trades without a planned risk are counted apart', () => {
		const c = coreStats([run('a', 1, [trade({ reason: 'open', net: 50 }), trade({ net: 10 }), trade({ net: -5, rBp: -5000 })])]);
		expect(c.trades).toBe(2);
		expect(c.withR).toBe(1);
		expect(c.withoutR).toBe(1);
		expect(c.wins).toBe(1);
	});

	it('R buckets have closed upper bounds', () => {
		expect([-3, -2, -1.5, -1, 0, 0.5, 1, 2, 2.01].map(rBucket)).toEqual([0, 0, 1, 1, 2, 3, 3, 4, 5]);
	});

	it('stops: respected over judged trades, moved away over perpetual trades that report it', () => {
		const c = coreStats([
			run('a', 1, [
				trade({ respected: true, movedAway: 0 }),
				trade({ respected: false, movedAway: 2 }),
				trade({ respected: null, movedAway: null, option: true }),
				trade({ reason: 'open', movedAway: 1 })
			])
		]);
		expect([c.stopsJudged, c.stopsRespected, c.respectedRate]).toEqual([2, 1, 0.5]);
		expect([c.stopsReported, c.movedAway, c.movedAwayRate]).toEqual([3, 2, 2 / 3]);
	});

	it('risk per trade against the 1–2% rule, oversized runs, liquidations and margin calls', () => {
		const c = coreStats([
			run('a', 1, [trade({ riskPct: 0.5 }), trade({ riskPct: 1 }), trade({ riskPct: 2 }), trade({ riskPct: 3.5, reason: 'liquidation' })], {
				findings: [{ code: 'oversized', tone: 'warn' }]
			}),
			run('b', 2, [trade({ reason: 'margin_call', option: true })])
		]);
		expect([c.riskTrades, c.riskBelow1, c.risk1to2, c.riskAbove2]).toEqual([4, 1, 2, 1]);
		expect(c.avgRiskPct).toBeCloseTo(1.75, 10);
		expect([c.oversizedRuns, c.liquidations, c.marginCalls]).toEqual([1, 1, 1]);
	});

	it('repeated mistakes: warning and bad findings by runs, flagged from two runs, with their lesson', () => {
		const rows = mistakes([
			run('a', 1, [], { findings: [{ code: 'no_stop', tone: 'bad' }, { code: 'no_stop', tone: 'bad' }, { code: 'waited', tone: 'info' }] }),
			run('b', 5, [], { findings: [{ code: 'no_stop', tone: 'bad' }, { code: 'oversized', tone: 'warn' }] })
		]);
		expect(rows).toEqual([
			{ code: 'no_stop', tone: 'bad', runs: 2, lastAt: 5, repeats: true, lesson: 'risk-sizing' },
			{ code: 'oversized', tone: 'warn', runs: 1, lastAt: 5, repeats: false, lesson: 'risk-sizing' }
		]);
	});

	it('groups by mission and by curriculum stage (a pack run is practice), and filters by mission', () => {
		const runs = [
			run('a', 1, [trade({ net: 1, rBp: 10000 })], { missionId: 'm06-thin-book' }),
			run('b', 2, [trade({ net: -1, rBp: -10000 })], { missionId: 'csv-1a2b' }),
			run('c', 3, [trade({ net: 1, rBp: 5000 })])
		];
		const s = journalStats(runs);
		expect(s.byMission.map((g) => g.key)).toEqual(['m06-thin-book', 'csv-1a2b', 'm01-false-breakout']);
		expect(s.byStage.map((g) => g.key)).toEqual(['structure', 'orders', PRACTICE_STAGE, 'futures1']);
		expect(journalStats(runs, 'm06-thin-book').core.runs).toBe(1);
		expect(s.tooFew).toBe(true);
	});

	it('trend: the last 10 runs against the 10 before them, and a point per run', () => {
		const runs = Array.from({ length: 25 }, (_, i) => run(`r${i}`, i, [trade({ net: i >= 15 ? 1 : -1, rBp: i >= 15 ? 10000 : -10000 })]));
		const t = trend(runs);
		expect(t.recent?.runs).toBe(10);
		expect(t.recent?.expectancyR).toBe(1);
		expect(t.previous?.expectancyR).toBe(-1);
		expect(t.points).toHaveLength(25);
		expect(trend(runs.slice(0, 4)).previous).toBeNull();
	});

	it('counts own runs with a summary only: assisted attempts and runs without a debrief are out', () => {
		const summary = run('x', 1, []).summary;
		const records = [
			{ runId: 'b', missionId: 'm01', createdAt: 2, summary, status: 'finished' as const },
			{ runId: 'a', missionId: 'm01', createdAt: 1, summary, status: 'finished' as const },
			{ runId: 'assisted', missionId: 'm01', createdAt: 3, summary, status: 'finished' as const, assisted: { from: 'a', kind: 'seed' as const } },
			{ runId: 'left', missionId: 'm01', createdAt: 4, status: 'left' as const },
			{ runId: 'old', missionId: 'm01', createdAt: 5, status: 'bankrupt' as const }
		];
		expect(statsRuns(records).map((r) => r.runId)).toEqual(['a', 'b']);
		expect(missingSummaries(records).map((r) => r.runId)).toEqual(['old']);
	});
});

describe('run summary from the engine debrief', () => {
	const tr = (p: Partial<EngineTrade>): EngineTrade =>
		({ n: 1, series: 0, side: 'long', entryStep: 1, exitStep: 5, qty: '1', entryAvg: '100', exitAvg: '98', pnl: '-2', fees: '0', funding: '0', net: '-150', bars: 4, reason: 'stop', plannedRisk: '150', rBp: -10000, planRespected: true, stopMovedAway: 1, ...p }) as EngineTrade;

	it('copies the engine numbers and derives the risk share from the initial capital', () => {
		const d = {
			mission: 'm07-trend-trailing',
			status: 'finished',
			finalEquity: '9850',
			netPnl: '-150',
			score: { version: 13, total: 64, dims: {} },
			findings: [{ code: 'no_plan', tone: 'warn' }],
			trades: [tr({}), tr({ series: 4, plannedRisk: '0', rBp: null, planRespected: null, stopMovedAway: undefined })]
		} as unknown as DebriefData;
		const s = summarize(d)!;
		expect(s.initial).toBe(10000);
		expect(s.trades[0]).toEqual({ net: -150, rBp: -10000, riskPct: 1.5, reason: 'stop', respected: true, movedAway: 1, option: false });
		expect(s.trades[1]).toMatchObject({ riskPct: null, movedAway: null, option: true });
		expect(s.findings).toEqual([{ code: 'no_plan', tone: 'warn' }]);
		expect(summarize({ ...d, status: 'active' })).toBeNull();
	});

	it('a debrief from a build without stopMovedAway reads as unknown, not zero', () => {
		const d = { mission: 'm01', status: 'finished', finalEquity: '10000', netPnl: '0', score: { total: null }, findings: [], trades: [tr({ stopMovedAway: undefined })] } as unknown as DebriefData;
		expect(summarize(d)!.trades[0].movedAway).toBeNull();
	});
});
