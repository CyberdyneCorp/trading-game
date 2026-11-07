import { describe, expect, it } from 'vitest';
import { loggedBars, nextStep, REPLAY_START, replayDone, type ReplayCursor } from './replay.ts';

type A = { k: string; n?: number; id?: number };

function steps(actions: A[]): { commands: string[]; bar: boolean }[] {
	const out: { commands: string[]; bar: boolean }[] = [];
	let c: ReplayCursor = REPLAY_START;
	while (!replayDone(actions, c)) {
		const s = nextStep(actions, c);
		out.push({ commands: s.commands.map((a) => a.k), bar: s.bar });
		c = s.next;
	}
	return out;
}

describe('replay viewer stepping (D6)', () => {
	it('applies the commands before an advance with its first bar, and plays an n-bar advance one bar at a time', () => {
		const log: A[] = [{ k: 'lesson' }, { k: 'plan' }, { k: 'order' }, { k: 'adv', n: 3 }, { k: 'close' }, { k: 'adv', n: 1 }];
		expect(steps(log)).toEqual([
			{ commands: ['lesson', 'plan', 'order'], bar: true },
			{ commands: [], bar: true },
			{ commands: [], bar: true },
			{ commands: ['close'], bar: true }
		]);
		expect(loggedBars(log)).toBe(4);
	});

	it('commands after the last advance are applied without a bar (a run exported mid-decision)', () => {
		expect(steps([{ k: 'adv', n: 2 }, { k: 'plan' }, { k: 'order' }])).toEqual([
			{ commands: [], bar: true },
			{ commands: [], bar: true },
			{ commands: ['plan', 'order'], bar: false }
		]);
	});

	it('an empty log is done at once', () => {
		expect(replayDone([], REPLAY_START)).toBe(true);
		expect(nextStep([], REPLAY_START)).toEqual({ commands: [], bar: false, next: { index: 0, left: 0 } });
	});
});
