import { describe, expect, it } from 'vitest';
import { assistAllowed, rewindPrefix, rewindRange } from './assisted.ts';

type A = { k: string; n?: number; id?: number };

describe('assisted practice: rewind prefix', () => {
	const log: A[] = [
		{ k: 'lesson' },
		{ k: 'adv', n: 3 }, // 60 -> 63
		{ k: 'order', id: 1 }, // at 63
		{ k: 'adv', n: 10 }, // 63 -> 73
		{ k: 'close', id: 2 }, // at 73
		{ k: 'adv', n: 1 }
	];

	it('keeps the commands before the target step and shortens the advance that crosses it', () => {
		expect(rewindPrefix(log, 60, 68)).toEqual([{ k: 'lesson' }, { k: 'adv', n: 3 }, { k: 'order', id: 1 }, { k: 'adv', n: 5 }]);
	});

	it('stops before the commands sent at the target step itself', () => {
		expect(rewindPrefix(log, 60, 63)).toEqual([{ k: 'lesson' }, { k: 'adv', n: 3 }]);
		expect(rewindPrefix(log, 60, 73)).toEqual(log.slice(0, 4));
	});

	it('rewinding to the first decision keeps only what was done before any advance', () => {
		expect(rewindPrefix(log, 60, 60)).toEqual([]);
	});

	it('never edits the original log', () => {
		const copy = structuredClone(log);
		rewindPrefix(log, 60, 65);
		expect(log).toEqual(copy);
	});

	it('offers the decisions before the current one, and none at the first', () => {
		expect(rewindRange(60, 60)).toBeNull();
		expect(rewindRange(60, 75)).toEqual({ min: 60, max: 74 });
	});

	it('is not offered for a campaign mission in progress, only after it ended', () => {
		expect(assistAllowed(true, 'active')).toBe(false);
		expect(assistAllowed(true, 'finished')).toBe(true);
		expect(assistAllowed(false, 'active')).toBe(true);
	});
});
