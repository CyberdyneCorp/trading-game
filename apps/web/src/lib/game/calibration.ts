// Calibration question scheduling on the client. The engine owns the schedule, due state,
// answers and Brier scoring (engine/scoring); the client only avoids advancing past a question.

import type { GameState } from '#lib/engine/types.ts';

/** Largest advance (<= n) that stops on the next question step, so it is asked before its bars exist. */
export function stepsUntilNextQuestion(state: GameState, n: number): number {
	const { every, horizon } = state.calibration;
	const w = state.warmupBars;
	if (state.step < w || every <= 0) return n;
	const next = w + (Math.floor((state.step - w) / every) + 1) * every;
	if (next + horizon >= state.totalBars) return n;
	return Math.min(n, next - state.step);
}
