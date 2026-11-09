// Debrief: the objectives of m22-m25 read against the engine's report-only facts (scoring v14
// episodes and bias findings; add-practice-and-advanced-strategies D12, D13, spec gameplay-loop
// "Debrief views of biases and episodes"). The engine computes every fact; this module only says
// which objective each fact meets or misses. An objective without a reading here is not judged.

import type { DebriefData, EngineFinding, ObjectiveCode, PositionEpisode } from '#lib/engine/types.ts';

export type ObjectiveVerdict = 'met' | 'missed';

export interface ObjectiveReading {
	code: ObjectiveCode;
	verdict: ObjectiveVerdict;
	/** The bar the reading points to (a finding's step), when it has one. */
	step?: number;
}

const has = (findings: EngineFinding[], code: EngineFinding['code']) => findings.find((f) => f.code === code);

// m22: total planned risk across the entries of one idea, in basis points of the initial balance.
function totalRisk(episodes: PositionEpisode[], valueBp: number, initial: number): ObjectiveVerdict {
	const limit = (initial * valueBp) / 10_000;
	return episodes.every((e) => Number(e.plannedRisk) > 0 && Number(e.plannedRisk) <= limit + 1e-9) ? 'met' : 'missed';
}

// m22: a partial exit (an exit before the last one) or a trailing stop in some episode.
const partialOrTrail = (episodes: PositionEpisode[]): ObjectiveVerdict =>
	episodes.some((e) => e.trailing || e.exits.length > 1 || (e.exitStep == null && e.exits.length > 0)) ? 'met' : 'missed';

function stopAway(episodes: PositionEpisode[]): ObjectiveReading['step'] | null {
	for (const e of episodes) for (const m of e.stopMoves) if (m.kind === 'away') return m.step;
	return null;
}

function byFinding(code: ObjectiveCode, f: EngineFinding | undefined): ObjectiveReading {
	return f ? { code, verdict: 'missed', step: f.step } : { code, verdict: 'met' };
}

function reading(code: ObjectiveCode, valueBp: number, d: DebriefData, initial: number): ObjectiveReading | null {
	const episodes = d.episodes ?? []; // absent from a pre-v14 engine's debrief
	const traded = episodes.length > 0;
	switch (code) {
		case 'total_risk_all_entries':
			return traded ? { code, verdict: totalRisk(episodes, valueBp, initial) } : null;
		case 'partial_exit_or_trail':
			return traded ? { code, verdict: partialOrTrail(episodes) } : null;
		case 'never_stop_away': {
			const step = stopAway(episodes);
			return step == null ? { code, verdict: 'met' } : { code, verdict: 'missed', step };
		}
		case 'cooldown_after_stop':
			return byFinding(code, has(d.findings, 'revenge_trade'));
		case 'plan_before_entry_atr_stop':
			return byFinding(code, has(d.findings, 'fomo_entry') ?? has(d.findings, 'no_plan') ?? has(d.findings, 'no_stop'));
		case 'no_add_beyond_plan':
			return byFinding(code, has(d.findings, 'averaging_down'));
		default:
			return null;
	}
}

/** The mission's objectives this debrief can judge, in the briefing's order. */
export function objectiveReadings(d: DebriefData, initialBalance: string | number): ObjectiveReading[] {
	const initial = Number(initialBalance);
	return d.missionRules.objectives.flatMap((o) => {
		const r = reading(o.code, o.valueBp, d, initial);
		return r ? [r] : [];
	});
}

/** The bar a scoring v14 finding points to on the chart (its evidence's entry or add, else its step). */
export function findingMoment(f: EngineFinding): number | null {
	if (!f.evidence) return null;
	return f.evidence.entryStep ?? f.evidence.addStep ?? f.step;
}
