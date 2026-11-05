import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import { IDBFactory } from 'fake-indexeddb';
import { settleCampaignRun, startCampaign } from './campaign.ts';
import { EMPTY_PROGRESS, migrateProgress, openDb, RunStore, type RunRecord } from './storage.ts';

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
		// The replaced run is no longer in progress (the journal showed it 'in progress' forever).
		const status = Object.fromEntries((await store.runs()).map((r) => [r.runId, r.status]));
		expect(status).toEqual({ old: 'left', new: 'active' });
	});

	it('a finished run stays finished when a new run replaces it', async () => {
		await store.createRun(record('done'));
		await store.updateRun('done', { status: 'finished' });
		await store.createRun({ ...record('next'), createdAt: 2 });
		expect((await store.runs()).find((r) => r.runId === 'done')?.status).toBe('finished');
	});

	it('leaves the active run on request (an ended campaign): it is no longer resumable', async () => {
		await store.createRun(record('camp'));
		await store.leaveActiveRun();
		expect((await store.activeRun())?.record.status).toBe('left');
	});

	it('reads a run replaced before runs were marked left as left', async () => {
		const tx = (store as unknown as { db: IDBDatabase }).db.transaction('runs', 'readwrite');
		tx.objectStore('runs').put({ ...record('legacy'), createdAt: 0 });
		await new Promise((r) => (tx.oncomplete = r));
		await store.createRun(record('current'));
		const legacy = (await store.runs()).find((r) => r.runId === 'legacy');
		expect(legacy?.status).toBe('left');
		expect((await store.runs()).find((r) => r.runId === 'current')?.status).toBe('active');
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

	it('an assisted attempt (rewind or another market) is not a completed mission', async () => {
		await store.createRun({ ...record('x'), missionId: 'm07-trend-trailing', status: 'finished', assisted: { from: 'a', kind: 'rewind', step: 70 } });
		await store.createRun({ ...record('y'), missionId: 'm08-regime-switch', status: 'finished', assisted: { from: 'b', kind: 'seed' } });
		expect(await store.finishedMissions()).toEqual([]);
	});

	it('regression: a finished practice run on an imported CSV pack is not a completed mission', async () => {
		await store.createRun({ ...record('p'), missionId: 'csv-abc123', status: 'finished', packDigest: 'd1' });
		expect(await store.finishedMissions()).toEqual([]);
	});

	it('keeps player progress (tutorials seen, advisor points) and starts empty with 3 points', async () => {
		expect(await store.progress()).toEqual(EMPTY_PROGRESS);
		await store.saveProgress({ ...EMPTY_PROGRESS, tutorialsSeen: ['m01-false-breakout'], advisorPoints: 5, settledRuns: ['r1'] });
		const p = await store.progress();
		expect(p.tutorialsSeen).toEqual(['m01-false-breakout']);
		expect(p.advisorPoints).toBe(5);
		expect(p.settledRuns).toEqual(['r1']);
	});

	it('reads a progress record saved before advisor points with the starting balance', async () => {
		await store.saveProgress({ tutorialsSeen: ['m02-funding-squeeze'] } as never);
		expect(await store.progress()).toEqual({ ...EMPTY_PROGRESS, tutorialsSeen: ['m02-funding-squeeze'] });
	});

	it('migrates a version-1 progress record: points and tutorials kept, no campaign, nothing assessed', async () => {
		await store.saveProgress({ tutorialsSeen: ['m01-false-breakout'], advisorPoints: 7, settledRuns: ['r9'] } as never);
		expect(await store.progress()).toEqual({
			version: 2,
			tutorialsSeen: ['m01-false-breakout'],
			advisorPoints: 7,
			settledRuns: ['r9'],
			perksIntroduced: [],
			campaign: null,
			campaignHistory: [],
			assessed: []
		});
	});

	it('keeps the campaign run, its history and the assessed missions across a save', async () => {
		const p = startCampaign(EMPTY_PROGRESS, 'c1', 5);
		const done = settleCampaignRun(p, { id: 'c1', index: 0 }, { runId: 'r1', missionId: 'm06-thin-book', result: 'finished', equity: '9500' });
		await store.saveProgress(done);
		const back = await store.progress();
		expect(back.campaign?.capital).toBe('9500');
		expect(back.campaign?.index).toBe(1);
		expect(back.assessed).toEqual(['m06-thin-book']);
	});

	it('drops damaged progress fields instead of failing', () => {
		const p = migrateProgress({ tutorialsSeen: 'x', advisorPoints: '4', campaign: { id: 3 }, campaignHistory: [null, 1], assessed: [1, 'm01-false-breakout'] });
		expect(p).toEqual({ ...EMPTY_PROGRESS, assessed: ['m01-false-breakout'] });
	});

	it('lists every run header newest first for the journal', async () => {
		await store.createRun({ ...record('old'), createdAt: 1, journal: ['a'] });
		await store.createRun({ ...record('new'), createdAt: 9, campaign: { id: 'c', index: 0 }, equity: '10000' });
		const runs = await store.runs();
		expect(runs.map((r) => r.runId)).toEqual(['new', 'old']);
		expect(runs[0].campaign).toEqual({ id: 'c', index: 0 });
		expect(runs[1].journal).toEqual(['a']);
	});

	it('stores packs by digest, lists them oldest first and deletes them', async () => {
		const pack = (digest: string, addedAt: number) => ({
			digest, scenarioId: `csv-${digest}`, label: digest, bytes: new Uint8Array([1, 2, 3]), private: true,
			totalBars: 120, warmupBars: 30, leverageCap: 5, gaps: 0, addedAt
		});
		await store.savePack(pack('b', 2));
		await store.savePack(pack('a', 1));
		expect((await store.packs()).map((p) => p.digest)).toEqual(['a', 'b']);
		expect((await store.pack('b'))?.bytes).toEqual(new Uint8Array([1, 2, 3]));
		await store.deletePack('a');
		expect(await store.pack('a')).toBeNull();
		await store.createRun({ ...record('p'), missionId: 'csv-b', packDigest: 'b' });
		expect((await store.activeRun())?.record.packDigest).toBe('b');
	});

	it('creates the device seed once and keeps it', async () => {
		let calls = 0;
		const random = () => (calls++, new Uint8Array(32).fill(0xab));
		const first = await store.deviceSeed(random);
		expect(first).toBe('ab'.repeat(32));
		expect(await store.deviceSeed(() => new Uint8Array(32))).toBe(first);
		expect(calls).toBe(1);
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

	it('keeps only the latest checkpoint of a run and drops those of runs that cannot resume', async () => {
		const cp = (runId: string, actions: number) => ({ runId, actions, hex: `hex${actions}`, digest: `d${actions}`, engine: '/e.mjs' });
		expect(await store.checkpoint('r1')).toBeNull();
		await store.saveCheckpoint(cp('r1', 20));
		await store.saveCheckpoint(cp('r1', 40));
		await store.saveCheckpoint(cp('r1', 20)); // a late write of an older one never wins
		expect(await store.checkpoint('r1')).toMatchObject({ actions: 40, hex: 'hex40', digest: 'd40' });
		await store.saveCheckpoint(cp('r2', 20));
		await store.pruneCheckpoints('r2');
		expect(await store.checkpoint('r1')).toBeNull();
		expect(await store.checkpoint('r2')).not.toBeNull();
		await store.pruneCheckpoints(null);
		expect(await store.checkpoint('r2')).toBeNull();
	});

	it('opens a version-2 database (no checkpoints store) and adds the store', async () => {
		globalThis.indexedDB = new IDBFactory();
		await new Promise<void>((resolve, reject) => {
			const open = indexedDB.open('tradelab', 2);
			open.onupgradeneeded = () => {
				open.result.createObjectStore('runs', { keyPath: 'runId' });
				open.result.createObjectStore('actions', { keyPath: ['runId', 'seq'] });
				open.result.createObjectStore('meta', { keyPath: 'key' });
				open.result.createObjectStore('packs', { keyPath: 'digest' });
			};
			open.onsuccess = () => {
				open.result.transaction('runs', 'readwrite').objectStore('runs').put(record('kept'));
				open.result.close();
				resolve();
			};
			open.onerror = () => reject(open.error);
		});
		const upgraded = new RunStore<Action, unknown, unknown>(await openDb());
		expect((await upgraded.runs()).map((r) => r.runId)).toEqual(['kept']);
		await upgraded.saveCheckpoint({ runId: 'kept', actions: 20, hex: 'h', digest: 'd', engine: '/e.mjs' });
		expect(await upgraded.checkpoint('kept')).not.toBeNull();
	});
});
