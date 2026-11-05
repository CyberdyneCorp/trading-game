// Campaign report (spec gameplay-loop "Campaign report"; design D8.2 m12, task 6.B8).
//
// Each settled campaign mission keeps a compact copy of its engine debrief on its campaign leg
// (`legReport`): every trade's net result, R multiple and entry regime, and the run's costs. The
// capstone's debrief aggregates those copies. Nothing here recomputes a trade's P&L: it only counts
// and sums what the engine reported.

import type { DebriefData, Totals } from '#lib/engine/types.ts';

/** Below this many trades the report says it is too early to conclude anything about an edge. */
export const REPORT_MIN_TRADES = 30;

export interface LegTrade {
	net: string;
	/** R multiple in basis points (10000 = 1R); null without a planned risk. */
	rBp: number | null;
	regime: string | null;
}

/** What the campaign report needs from one mission's engine debrief. */
export interface LegReport {
	trades: LegTrade[];
	netPnl: string;
	fees: string;
	spreadSlippage: string;
	/** Funding received (+) or paid (-). */
	funding: string;
}

export function legReport(d: Pick<DebriefData, 'trades' | 'netPnl' | 'spreadSlippage'>, totals: Pick<Totals, 'feesPaid' | 'fundingNet'>): LegReport {
	return {
		trades: d.trades.map((tr) => ({ net: tr.net, rBp: tr.rBp, regime: tr.regime ?? null })),
		netPnl: d.netPnl,
		fees: totals.feesPaid,
		spreadSlippage: d.spreadSlippage,
		funding: totals.fundingNet
	};
}

export interface RegimeResult {
	regime: string;
	trades: number;
	wins: number;
	net: number;
}

export interface CampaignReport {
	/** Missions whose debrief is stored. */
	missions: number;
	trades: number;
	wins: number;
	winRate: number | null;
	/** Average R of winning and of losing trades that have an R; null when there are none. */
	avgWinR: number | null;
	avgLossR: number | null;
	/** Mean R per trade (trades with an R). */
	expectancyR: number | null;
	/** Trades without an R (no planned risk), left out of the R averages. */
	withoutR: number;
	/** Fees, spread and slippage, and funding paid. */
	costs: number;
	/** Net P&L plus the costs. */
	gross: number;
	/** Costs over gross P&L; null when the gross P&L is not positive. */
	costShare: number | null;
	byRegime: RegimeResult[];
	tooFew: boolean;
}

const mean = (xs: number[]): number | null => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);

function costsOf(r: LegReport): number {
	return Number(r.fees) + Number(r.spreadSlippage) + Math.max(0, -Number(r.funding));
}

function byRegime(trades: LegTrade[]): RegimeResult[] {
	const rows = new Map<string, RegimeResult>();
	for (const tr of trades) {
		const regime = tr.regime ?? 'unlabeled';
		const row = rows.get(regime) ?? { regime, trades: 0, wins: 0, net: 0 };
		row.trades += 1;
		row.wins += Number(tr.net) > 0 ? 1 : 0;
		row.net += Number(tr.net);
		rows.set(regime, row);
	}
	return [...rows.values()].sort((a, b) => b.trades - a.trades);
}

/** Aggregates the stored debriefs of a campaign run's settled missions (legs without one are skipped). */
export function campaignReport(legs: readonly { report?: LegReport }[]): CampaignReport {
	const reports = legs.map((l) => l.report).filter((r): r is LegReport => r != null);
	const trades = reports.flatMap((r) => r.trades);
	const withR = trades.filter((tr) => tr.rBp != null).map((tr) => (tr.rBp as number) / 10000);
	const wins = trades.filter((tr) => Number(tr.net) > 0).length;
	const costs = reports.reduce((a, r) => a + costsOf(r), 0);
	const gross = reports.reduce((a, r) => a + Number(r.netPnl), 0) + costs;
	return {
		missions: reports.length,
		trades: trades.length,
		wins,
		winRate: trades.length ? wins / trades.length : null,
		avgWinR: mean(withR.filter((r) => r > 0)),
		avgLossR: mean(withR.filter((r) => r <= 0)),
		expectancyR: mean(withR),
		withoutR: trades.length - withR.length,
		costs,
		gross,
		costShare: gross > 0 ? costs / gross : null,
		byRegime: byRegime(trades),
		tooFew: trades.length < REPORT_MIN_TRADES
	};
}
