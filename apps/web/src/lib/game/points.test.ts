import { describe, expect, it } from 'vitest';
import { allowanceFor, MAX_POINTS, settleRun, START_POINTS } from './points.ts';
import { EMPTY_PROGRESS } from './storage.ts';

describe('advisor points', () => {
	it('starts at 3 and the allowance is the balance', () => {
		expect(EMPTY_PROGRESS.advisorPoints).toBe(START_POINTS);
		expect(allowanceFor(EMPTY_PROGRESS)).toBe(3);
	});

	it('settles balance - used + earned once per run (idempotent across reloads and replays)', () => {
		const once = settleRun(EMPTY_PROGRESS, 'run-1', 2, 4);
		expect(once.advisorPoints).toBe(5);
		const twice = settleRun(once, 'run-1', 2, 4);
		expect(twice).toBe(once);
		expect(settleRun(once, 'run-2', 1, 0).advisorPoints).toBe(4);
	});

	it('is capped at 10 and never negative', () => {
		let p = { ...EMPTY_PROGRESS, advisorPoints: 9 };
		p = settleRun(p, 'a', 0, 6);
		expect(p.advisorPoints).toBe(MAX_POINTS);
		expect(settleRun({ ...EMPTY_PROGRESS, advisorPoints: 1 }, 'b', 3, 0).advisorPoints).toBe(0);
	});

	it('keeps the other progress fields', () => {
		const p = settleRun({ ...EMPTY_PROGRESS, tutorialsSeen: ['m01-false-breakout'] }, 'r', 0, 2);
		expect(p.tutorialsSeen).toEqual(['m01-false-breakout']);
	});

	it('ignores a missing run id', () => {
		expect(settleRun(EMPTY_PROGRESS, '', 0, 6)).toBe(EMPTY_PROGRESS);
	});
});
