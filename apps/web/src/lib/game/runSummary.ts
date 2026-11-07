// Per-run summary for the journal statistics (add-run-sharing-and-journal-stats design D7): a compact
// copy of what the engine's debrief says about one run, stored on its run record when the debrief
// loads, so the statistics screen never replays runs. Nothing here recomputes a trade: it copies the
// engine's numbers and derives the risk share from the planned risk and the initial capital.

import type { DebriefData, ExitReason, FindingCode, FindingTone } from '#lib/engine/types.ts';

export interface TradeSummary {
	net: number;
	/** R multiple in basis points (10000 = 1R); null without a planned risk or while open. */
	rBp: number | null;
	/** Planned risk as a percentage of the run's initial capital; null without a planned risk. */
	riskPct: number | null;
	reason: ExitReason;
	/** Engine's planRespected: null when it could not judge (no invalidation, option, open). */
	respected: boolean | null;
	/** Engine's stopMovedAway (perpetual trades); null for options and debriefs that predate it. */
	movedAway: number | null;
	option: boolean;
}

export interface RunSummary {
	v: 1;
	mission: string;
	status: 'finished' | 'bankrupt';
	initial: number;
	net: number;
	score: number | null;
	trades: TradeSummary[];
	findings: { code: FindingCode; tone: FindingTone }[];
}

/** The summary of an ended run's debrief; null while the run is active. */
export function summarize(d: DebriefData): RunSummary | null {
	if (d.status !== 'finished' && d.status !== 'bankrupt') return null;
	const net = Number(d.netPnl);
	const initial = Number(d.finalEquity) - net; // the engine's netPnl is equity minus initial capital
	return {
		v: 1,
		mission: d.mission,
		status: d.status,
		initial,
		net,
		score: d.score.total,
		trades: d.trades.map((tr) => {
			const risk = Number(tr.plannedRisk);
			return {
				net: Number(tr.net),
				rBp: tr.rBp,
				riskPct: risk > 0 && initial > 0 ? (risk / initial) * 100 : null,
				reason: tr.reason,
				respected: tr.planRespected,
				movedAway: tr.series === 0 ? (tr.stopMovedAway ?? null) : null,
				option: tr.series !== 0
			};
		}),
		findings: d.findings.map((f) => ({ code: f.code, tone: f.tone }))
	};
}
