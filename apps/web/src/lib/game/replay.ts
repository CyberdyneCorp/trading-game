// Replay viewer stepping (add-run-sharing-and-journal-stats design D6): which logged actions the
// viewer applies for "next bar". Commands up to the next advance are applied, then one bar; an
// {"k":"adv","n":5} plays as five single bars (the engine's n single steps equal one n-step batch).

type Logged = { k: string; n?: number };

/** Where the viewer is in the log: the next action, and the bars still to play of an advance there. */
export interface ReplayCursor {
	index: number;
	/** Bars left of the advance at `index` once it started (0 = not started). */
	left: number;
}

export const REPLAY_START: ReplayCursor = { index: 0, left: 0 };

export interface ReplayStep<A> {
	/** Commands to apply before the bar, in log order. */
	commands: A[];
	/** True when one bar is to be advanced after them. */
	bar: boolean;
	next: ReplayCursor;
}

/** The next viewer step from `cursor`: the commands before the next advance, then one bar of it. */
export function nextStep<A extends Logged>(actions: readonly A[], cursor: ReplayCursor): ReplayStep<A> {
	const commands: A[] = [];
	for (let i = cursor.index; i < actions.length; i++) {
		const a = actions[i];
		if (a.k !== 'adv') {
			commands.push(a);
			continue;
		}
		const left = (i === cursor.index && cursor.left > 0 ? cursor.left : Math.max(1, a.n ?? 1)) - 1;
		return { commands, bar: true, next: left > 0 ? { index: i, left } : { index: i + 1, left: 0 } };
	}
	return { commands, bar: false, next: { index: actions.length, left: 0 } };
}

export function replayDone(actions: readonly Logged[], cursor: ReplayCursor): boolean {
	return cursor.index >= actions.length && cursor.left === 0;
}

/** Bars the log advances in all (the engine may end the run before them). */
export function loggedBars(actions: readonly Logged[]): number {
	return actions.reduce((n, a) => n + (a.k === 'adv' ? Math.max(1, a.n ?? 1) : 0), 0);
}
