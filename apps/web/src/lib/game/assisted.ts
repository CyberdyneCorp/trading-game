// Assisted practice (task 4.8, spec gameplay-loop "Assisted practice"): rewinding a run or changing its
// market starts a new attempt flagged `assisted`. The original attempt and its score stay as they are;
// an assisted attempt never counts for progress (completed missions, intel perks), advisor points or
// the campaign, and it starts without the advisor.

/** How an assisted attempt was made, stored on its run record. */
export interface AssistedLink {
	/** The run it was made from. */
	from: string;
	kind: 'rewind' | 'seed';
	/** Rewind: the decision step the new attempt resumes at. */
	step?: number;
}

type LoggedAction = { k: string; n?: number };

/**
 * The prefix of an action log that brings a run from `startStep` back to the decision at `toStep`,
 * before anything was done there: commands at earlier steps are kept, an advance that crosses
 * `toStep` is shortened to end on it, and nothing after it is kept.
 */
export function rewindPrefix<A extends LoggedAction>(actions: readonly A[], startStep: number, toStep: number): A[] {
	const out: A[] = [];
	let step = startStep;
	for (const a of actions) {
		if (step >= toStep) break;
		if (a.k !== 'adv') {
			out.push(a);
			continue;
		}
		const n = a.n ?? 1;
		out.push(step + n > toStep ? { ...a, n: toStep - step } : a);
		step += n;
	}
	return out;
}

/** The decision steps a run can rewind to: from its first decision up to (not including) the current one. */
export function rewindRange(warmupBars: number, step: number): { min: number; max: number } | null {
	return step > warmupBars ? { min: warmupBars, max: step - 1 } : null;
}

/** Assisted practice is offered outside the campaign (a campaign mission in progress cannot be retried). */
export function assistAllowed(activeCampaignRun: boolean, status: string): boolean {
	return !(activeCampaignRun && status === 'active');
}
