// Advisor point balance (spec progression-rewards "Local point balance", design D3).
//
// The engine reports what a run earned (debrief advisorPointsEarned) and how much advice it used;
// the device keeps the balance. It starts at 3, is capped at 10 and is never purchasable. A run's
// advice allowance is the balance when it starts; when it ends the balance becomes
// balance - used + earned, applied once per run id (settledRuns), even after reloads or replays.

export const START_POINTS = 3;
export const MAX_POINTS = 10;
/** Settled run ids kept for idempotency; older ids belong to runs that can no longer be resumed. */
const KEEP_SETTLED = 100;

export interface PointsState {
	advisorPoints: number;
	settledRuns: string[];
}

const clamp = (n: number) => Math.max(0, Math.min(MAX_POINTS, Math.floor(n)));

/** The allowance a new run starts with. */
export function allowanceFor(p: PointsState): number {
	return clamp(p.advisorPoints);
}

/** Applies a finished run once: balance - used + earned, capped. Returns the same object if already settled. */
export function settleRun<P extends PointsState>(p: P, runId: string, used: number, earned: number): P {
	if (!runId || p.settledRuns.includes(runId)) return p;
	return {
		...p,
		advisorPoints: clamp(p.advisorPoints - used + earned),
		settledRuns: [...p.settledRuns, runId].slice(-KEEP_SETTLED)
	};
}
