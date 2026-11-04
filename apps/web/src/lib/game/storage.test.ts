import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import { IDBFactory } from 'fake-indexeddb';
import { openDb, RunStore, type RunRecord } from './storage.ts';

type Action = { k: string; n?: number };

function record(runId: string): RunRecord {
	return {
		runId, missionId: 'm01-false-breakout', seed: 7, createdAt: 1, executionPolicy: 3, journal: [], questions: [],
		stopWarnings: 0, digest: '', status: 'active', actions: 0
	};
}

describe('RunStore (IndexedDB)', () => {
	let store: RunStore<Action, unknown, unknown>;
	beforeEach(async () => {
		globalThis.indexedDB = new IDBFactory(); // isolated database per test
		store = new RunStore(await openDb());
	});

	it('returns no active run on a fresh database', async () => {
		expect(await store.activeRun()).toBeNull();
	});

	it('appends actions in order and resumes the active run', async () => {
		await store.createRun(record('r1'));
		await store.appendAction('r1', 0, { k: 'lev' });
		await store.appendAction('r1', 1, { k: 'adv', n: 3 });
		await store.updateRun('r1', { digest: 'abc' });
		const run = await store.activeRun();
		expect(run?.actions).toEqual([{ k: 'lev' }, { k: 'adv', n: 3 }]);
		expect(run?.record.digest).toBe('abc');
		expect(run?.record.actions).toBe(2);
	});

	it('a new run becomes the active one; the old one is kept', async () => {
		await store.createRun(record('old'));
		await store.appendAction('old', 0, { k: 'close' });
		await store.createRun(record('new'));
		const run = await store.activeRun();
		expect(run?.record.runId).toBe('new');
		expect(run?.actions).toEqual([]);
	});

	it('trusts only a gap-free prefix of the action log', async () => {
		await store.createRun(record('r'));
		await store.appendAction('r', 0, { k: 'a' });
		await store.appendAction('r', 2, { k: 'c' }); // simulated hole at seq 1
		expect((await store.activeRun())?.actions).toEqual([{ k: 'a' }]);
	});

	it('lists missions with a finished run; active and bankrupt runs do not count', async () => {
		await store.createRun({ ...record('a'), missionId: 'm01-false-breakout', status: 'finished' });
		await store.createRun({ ...record('b'), missionId: 'm01-false-breakout', status: 'finished' });
		await store.createRun({ ...record('c'), missionId: 'm02-funding-squeeze', status: 'bankrupt' });
		await store.createRun({ ...record('d'), missionId: 'm03-volatility-shock' });
		expect(await store.finishedMissions()).toEqual(['m01-false-breakout']);
		await store.updateRun('d', { status: 'finished' });
		expect((await store.finishedMissions()).sort()).toEqual(['m01-false-breakout', 'm03-volatility-shock']);
	});

	it('keeps player progress (tutorials seen) and starts empty', async () => {
		expect(await store.progress()).toEqual({ tutorialsSeen: [] });
		await store.saveProgress({ tutorialsSeen: ['m01-false-breakout'] });
		expect((await store.progress()).tutorialsSeen).toEqual(['m01-false-breakout']);
	});

	it('migrates the legacy localStorage save and removes it', async () => {
		const mem = new Map<string, string>();
		const storage = {
			getItem: (k: string) => mem.get(k) ?? null,
			setItem: (k: string, v: string) => void mem.set(k, v),
			removeItem: (k: string) => void mem.delete(k)
		} as unknown as Storage;
		storage.setItem('tradelab.run.v1', JSON.stringify({ missionId: 'm02-funding-squeeze', seed: 9, log: [{ k: 'adv', n: 1 }], digest: 'd' }));
		expect(await store.migrateLegacy(storage, 3)).toBe(true);
		const run = await store.activeRun();
		expect(run?.record.missionId).toBe('m02-funding-squeeze');
		expect(run?.actions).toEqual([{ k: 'adv', n: 1 }]);
		expect(storage.getItem('tradelab.run.v1')).toBeNull();
	});
});
