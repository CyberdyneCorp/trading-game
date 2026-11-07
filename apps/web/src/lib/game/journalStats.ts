// Journal statistics (add-run-sharing-and-journal-stats design D7): pure aggregation of the per-run
// summaries (runSummary.ts) of the player's own runs. Each number is framed by the lesson it measures
// (`STAT_LESSONS`, `FINDING_LESSONS`); none of it is a statement about the market. The desktop ports
// these functions and checks them against the web parity fixtures.

import type { FindingCode, FindingTone } from '#lib/engine/types.ts';
import { REPORT_MIN_TRADES } from './campaignReport.ts';
import { stagesOf } from './curriculum.ts';
import type { RunSummary, TradeSummary } from './runSummary.ts';
import type { RunRecord } from './storage.ts';

/** One counted run: a summary of the player's own run that reached its debrief. */
export interface StatsRun {
	runId: string;
	missionId: string;
	createdAt: number;
	summary: RunSummary;
}

/** Runs counted by the statistics, oldest first: own runs with a summary, never assisted (D7). */
export function statsRuns(records: readonly Pick<RunRecord, 'runId' | 'missionId' | 'createdAt' | 'assisted' | 'summary'>[]): StatsRun[] {
	return records
		.filter((r): r is typeof r & { summary: RunSummary } => !!r.summary && !r.assisted)
		.map((r) => ({ runId: r.runId, missionId: r.missionId, createdAt: r.createdAt, summary: r.summary }))
		.sort((a, b) => a.createdAt - b.createdAt);
}

/** Ended own runs that have no summary yet (recomputable through the engine, D7). */
export function missingSummaries<R extends Pick<RunRecord, 'status' | 'assisted' | 'summary'>>(records: readonly R[]): R[] {
	return records.filter((r) => !r.summary && !r.assisted && (r.status === 'finished' || r.status === 'bankrupt'));
}

/** R buckets: ≤ −2, (−2, −1], (−1, 0], (0, 1], (1, 2], > 2. */
export const R_BUCKETS = ['le-2', '-2..-1', '-1..0', '0..1', '1..2', 'gt2'] as const;
const BUCKET_UPPER = [-2, -1, 0, 1, 2];

export function rBucket(r: number): number {
	const i = BUCKET_UPPER.findIndex((upper) => r <= upper);
	return i < 0 ? BUCKET_UPPER.length : i;
}

export interface CoreStats {
	runs: number;
	/** Closed trades (an exit happened); open trades at the end have no R. */
	trades: number;
	wins: number;
	winRate: number | null;
	/** Closed trades with an R, and without one (no planned risk). */
	withR: number;
	withoutR: number;
	/** Mean R of closed trades with an R. */
	expectancyR: number | null;
	avgWinR: number | null;
	avgLossR: number | null;
	rBuckets: number[];
	stopsJudged: number;
	stopsRespected: number;
	respectedRate: number | null;
	/** Perpetual trades whose debrief reports stop moves, and those with a stop moved away. */
	stopsReported: number;
	movedAway: number;
	movedAwayRate: number | null;
	/** Trades with a planned risk, their average share of initial capital (%), and the bands. */
	riskTrades: number;
	avgRiskPct: number | null;
	riskBelow1: number;
	risk1to2: number;
	riskAbove2: number;
	oversizedRuns: number;
	liquidations: number;
	marginCalls: number;
}

const mean = (xs: readonly number[]): number | null => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);
const ratio = (n: number, d: number): number | null => (d > 0 ? n / d : null);
const count = <T>(xs: readonly T[], p: (x: T) => boolean): number => xs.reduce((n, x) => n + (p(x) ? 1 : 0), 0);

function rStats(closed: readonly TradeSummary[]) {
	const rs = closed.flatMap((t) => (t.rBp == null ? [] : [t.rBp / 10000]));
	const buckets: number[] = R_BUCKETS.map(() => 0);
	for (const r of rs) buckets[rBucket(r)] += 1;
	return {
		withR: rs.length,
		withoutR: closed.length - rs.length,
		expectancyR: mean(rs),
		avgWinR: mean(rs.filter((r) => r > 0)),
		avgLossR: mean(rs.filter((r) => r <= 0)),
		rBuckets: buckets
	};
}

function stopStats(closed: readonly TradeSummary[], all: readonly TradeSummary[]) {
	const judged = closed.filter((t) => t.respected != null);
	const reported = all.filter((t) => t.movedAway != null);
	const respected = count(judged, (t) => t.respected === true);
	const moved = count(reported, (t) => (t.movedAway ?? 0) > 0);
	return {
		stopsJudged: judged.length,
		stopsRespected: respected,
		respectedRate: ratio(respected, judged.length),
		stopsReported: reported.length,
		movedAway: moved,
		movedAwayRate: ratio(moved, reported.length)
	};
}

function riskStats(all: readonly TradeSummary[]) {
	const risks = all.flatMap((t) => (t.riskPct == null ? [] : [t.riskPct]));
	return {
		riskTrades: risks.length,
		avgRiskPct: mean(risks),
		riskBelow1: count(risks, (r) => r < 1),
		risk1to2: count(risks, (r) => r >= 1 && r <= 2),
		riskAbove2: count(risks, (r) => r > 2)
	};
}

export function coreStats(runs: readonly StatsRun[]): CoreStats {
	const all = runs.flatMap((r) => r.summary.trades);
	const closed = all.filter((t) => t.reason !== 'open');
	const wins = count(closed, (t) => t.net > 0);
	return {
		runs: runs.length,
		trades: closed.length,
		wins,
		winRate: ratio(wins, closed.length),
		...rStats(closed),
		...stopStats(closed, all),
		...riskStats(all),
		oversizedRuns: count(runs, (r) => r.summary.findings.some((f) => f.code === 'oversized')),
		liquidations: count(closed, (t) => t.reason === 'liquidation'),
		marginCalls: count(closed, (t) => t.reason === 'margin_call')
	};
}

/** The Academy lesson a statistic measures (design D7). */
export const STAT_LESSONS = {
	outcome: 'risk-expectancy',
	risk: 'risk-sizing',
	stops: 'mind-plan',
	liquidations: 'futures-margin',
	marginCalls: 'options-writing'
} as const;

/** The lesson a repeated debrief finding points to. */
export const FINDING_LESSONS: Partial<Record<FindingCode, string>> = {
	liquidated: 'futures-margin',
	stop_beyond_liq: 'futures-margin',
	leverage_vs_stop: 'futures-margin',
	liq_buffer: 'futures-margin',
	no_stop: 'risk-sizing',
	oversized: 'risk-sizing',
	bankrupt: 'risk-sizing',
	drawdown_budget: 'risk-expectancy',
	no_plan: 'mind-plan',
	lucky_win: 'mind-plan',
	margin_called: 'options-writing',
	wrote_options: 'options-writing',
	theta_decay: 'options-greeks',
	iv_crush: 'options-iv',
	iv_rise: 'options-iv',
	funding_cost: 'futures-funding',
	gap_slippage: 'orders-execution',
	capacity: 'orders-execution'
};

export interface MistakeRow {
	code: FindingCode;
	tone: FindingTone;
	/** Runs whose debrief had it. */
	runs: number;
	lastAt: number;
	/** In at least two runs: a pattern, not a one-off. */
	repeats: boolean;
	lesson: string | null;
}

/** Warning and bad findings by how many runs had them, most frequent first. */
export function mistakes(runs: readonly StatsRun[]): MistakeRow[] {
	const rows = new Map<FindingCode, MistakeRow>();
	for (const r of runs) {
		const codes = new Map(r.summary.findings.filter((f) => f.tone === 'bad' || f.tone === 'warn').map((f) => [f.code, f.tone]));
		for (const [code, tone] of codes) {
			const row = rows.get(code) ?? { code, tone, runs: 0, lastAt: 0, repeats: false, lesson: FINDING_LESSONS[code] ?? null };
			row.runs += 1;
			row.lastAt = Math.max(row.lastAt, r.createdAt);
			row.repeats = row.runs >= 2;
			rows.set(code, row);
		}
	}
	return [...rows.values()].sort((a, b) => b.runs - a.runs || b.lastAt - a.lastAt);
}

export interface GroupRow {
	key: string;
	runs: number;
	trades: number;
	winRate: number | null;
	expectancyR: number | null;
}

function groupBy(runs: readonly StatsRun[], keys: (r: StatsRun) => string[]): GroupRow[] {
	const groups = new Map<string, StatsRun[]>();
	for (const r of runs) for (const k of keys(r)) groups.set(k, [...(groups.get(k) ?? []), r]);
	return [...groups.entries()].map(([key, rs]) => {
		const c = coreStats(rs);
		return { key, runs: c.runs, trades: c.trades, winRate: c.winRate, expectancyR: c.expectancyR };
	});
}

/** Practice runs on packs have no curriculum stage. */
export const PRACTICE_STAGE = 'practice';

export const byMission = (runs: readonly StatsRun[]): GroupRow[] => groupBy(runs, (r) => [r.missionId]);
export const byStage = (runs: readonly StatsRun[]): GroupRow[] =>
	groupBy(runs, (r) => {
		const stages = stagesOf(r.missionId);
		return stages.length ? stages : [PRACTICE_STAGE];
	});

/** The trend compares the last TREND_WINDOW runs with the TREND_WINDOW before them. */
export const TREND_WINDOW = 10;

export interface TrendPoint {
	runId: string;
	missionId: string;
	createdAt: number;
	trades: number;
	expectancyR: number | null;
	respectedRate: number | null;
	score: number | null;
}

export interface Trend {
	recent: CoreStats | null;
	previous: CoreStats | null;
	points: TrendPoint[];
}

export function trend(runs: readonly StatsRun[]): Trend {
	const recent = runs.slice(-TREND_WINDOW);
	const previous = runs.slice(-2 * TREND_WINDOW, -TREND_WINDOW);
	return {
		recent: recent.length ? coreStats(recent) : null,
		previous: previous.length ? coreStats(previous) : null,
		points: runs.map((r) => {
			const c = coreStats([r]);
			return { runId: r.runId, missionId: r.missionId, createdAt: r.createdAt, trades: c.trades, expectancyR: c.expectancyR, respectedRate: c.respectedRate, score: r.summary.score };
		})
	};
}

export interface JournalStats {
	core: CoreStats;
	mistakes: MistakeRow[];
	byMission: GroupRow[];
	byStage: GroupRow[];
	trend: Trend;
	/** Fewer than REPORT_MIN_TRADES closed trades: too early to conclude anything. */
	tooFew: boolean;
}

/** Everything the statistics screen shows, optionally for one mission. */
export function journalStats(all: readonly StatsRun[], missionId: string | null = null): JournalStats {
	const runs = missionId ? all.filter((r) => r.missionId === missionId) : all;
	const core = coreStats(runs);
	return { core, mistakes: mistakes(runs), byMission: byMission(runs), byStage: byStage(runs), trend: trend(runs), tooFew: core.trades < REPORT_MIN_TRADES };
}
