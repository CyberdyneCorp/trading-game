// Calibration questions (spec mission-scoring "Calibration"): binary questions that resolve inside
// the mission, asked before the resolving bars exist on the client. Scored with the Brier score.

import type { GameState } from '#lib/engine/types.ts';

export const QUESTION_EVERY = 15; // bars between questions
export const QUESTION_HORIZON = 5; // bars until resolution
export const MIN_RESOLVED = 3; // fewer => dimension not applicable

export interface CalibrationQuestion {
	step: number; // decision step when asked
	threshold: string; // close at that step (engine decimal)
	horizon: number;
	p: number | null; // probability the close at step+horizon is above threshold; null = skipped
}

/** Next step at which a question is due, or null when no question can still resolve. */
export function nextQuestionStep(state: GameState, asked: CalibrationQuestion[]): number | null {
	const first = state.warmupBars;
	const lastResolvable = state.totalBars - 1 - QUESTION_HORIZON;
	for (let s = first; s <= lastResolvable; s += QUESTION_EVERY) {
		if (s >= state.step && !asked.some((q) => q.step === s)) return s;
	}
	return null;
}

export function questionDue(state: GameState, asked: CalibrationQuestion[]): boolean {
	return state.status === 'active' && nextQuestionStep(state, asked) === state.step;
}

/** Outcome (1 above / 0 not) once the resolving bar is visible; null otherwise. */
export function outcome(q: CalibrationQuestion, bars: GameState['bars']): 0 | 1 | null {
	const bar = bars[q.step + q.horizon];
	if (!bar) return null;
	return Number(bar[3]) > Number(q.threshold) ? 1 : 0;
}

export interface CalibrationResult {
	resolved: number;
	brier: number | null;
	score: number | null; // 100 * (1 - Brier); always answering 50% scores 75
}

export function calibrationScore(questions: CalibrationQuestion[], bars: GameState['bars']): CalibrationResult {
	const pairs = questions
		.filter((q) => q.p != null)
		.map((q) => ({ p: q.p as number, o: outcome(q, bars) }))
		.filter((x): x is { p: number; o: 0 | 1 } => x.o != null);
	if (pairs.length < MIN_RESOLVED) return { resolved: pairs.length, brier: null, score: null };
	const brier = pairs.reduce((a, x) => a + (x.p - x.o) ** 2, 0) / pairs.length;
	return { resolved: pairs.length, brier, score: Math.round(100 * (1 - brier)) };
}
