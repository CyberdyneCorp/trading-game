// IndexedDB persistence for runs (spec web-client "Local persistence", offline-sync "Durable decisions").
//
// A run is a header (mission, seed, policy, journal, ...) plus an append-only action log keyed by
// (runId, seq). Each action is committed in its own transaction BEFORE it is sent to the engine,
// so anything the UI showed as accepted survives a crash or a closed tab. Resume replays the log
// through the deterministic engine; the stored digest only verifies the replay.

import { START_POINTS } from './points.ts';

export interface RunRecord<J = unknown, Q = unknown> {
	runId: string;
	missionId: string;
	seed: number;
	createdAt: number;
	executionPolicy: number;
	journal: J[];
	questions: Q[];
	stopWarnings: number;
	digest: string;
	status: 'active' | 'finished' | 'bankrupt';
	actions: number; // number of committed actions
	/** Start options (advice allowance, perks); absent on runs recorded before policy v11 = defaults. */
	options?: { adviceAllowance: number; newsLead: number; calendar: boolean };
}

export interface StoredRun<A, J, Q> {
	record: RunRecord<J, Q>;
	actions: A[];
}

/** Per-device player progress (specs mission-tutorial and progression-rewards). */
export interface Progress {
	tutorialsSeen: string[];
	/** Advisor point balance (see points.ts). */
	advisorPoints: number;
	/** Runs whose points were already settled (idempotency). */
	settledRuns: string[];
}

export const EMPTY_PROGRESS: Progress = { tutorialsSeen: [], advisorPoints: START_POINTS, settledRuns: [] };

const DB_NAME = 'tradelab';
const DB_VERSION = 1;
const LEGACY_KEY = 'tradelab.run.v1';

function req<T>(r: IDBRequest<T>): Promise<T> {
	return new Promise((resolve, reject) => {
		r.onsuccess = () => resolve(r.result);
		r.onerror = () => reject(r.error);
	});
}

function done(tx: IDBTransaction): Promise<void> {
	return new Promise((resolve, reject) => {
		tx.oncomplete = () => resolve();
		tx.onerror = () => reject(tx.error);
		tx.onabort = () => reject(tx.error ?? new Error('transaction aborted'));
	});
}

export function openDb(factory: IDBFactory = indexedDB): Promise<IDBDatabase> {
	return new Promise((resolve, reject) => {
		const open = factory.open(DB_NAME, DB_VERSION);
		open.onupgradeneeded = () => {
			const db = open.result;
			if (!db.objectStoreNames.contains('runs')) db.createObjectStore('runs', { keyPath: 'runId' });
			if (!db.objectStoreNames.contains('actions')) db.createObjectStore('actions', { keyPath: ['runId', 'seq'] });
			if (!db.objectStoreNames.contains('meta')) db.createObjectStore('meta', { keyPath: 'key' });
		};
		open.onsuccess = () => resolve(open.result);
		open.onerror = () => reject(open.error);
	});
}

export function newRunId(): string {
	const rnd = typeof crypto !== 'undefined' && 'randomUUID' in crypto ? crypto.randomUUID() : String(Math.random()).slice(2);
	return `run-${Date.now().toString(36)}-${rnd}`;
}

export class RunStore<A, J, Q> {
	constructor(private db: IDBDatabase) {}

	async createRun(record: RunRecord<J, Q>): Promise<void> {
		const tx = this.db.transaction(['runs', 'meta'], 'readwrite');
		tx.objectStore('runs').put(record);
		tx.objectStore('meta').put({ key: 'active', runId: record.runId });
		await done(tx);
	}

	/** Durably appends one action; resolves only after the transaction committed. */
	async appendAction(runId: string, seq: number, action: A): Promise<void> {
		const tx = this.db.transaction(['actions', 'runs'], 'readwrite');
		tx.objectStore('actions').put({ runId, seq, action });
		const runs = tx.objectStore('runs');
		const rec = (await req(runs.get(runId))) as RunRecord<J, Q> | undefined;
		if (rec) runs.put({ ...rec, actions: Math.max(rec.actions, seq + 1) });
		await done(tx);
	}

	/** Updates header fields (journal, questions, digest, status); never touches the action log. */
	async updateRun(runId: string, patch: Partial<Omit<RunRecord<J, Q>, 'runId' | 'actions'>>): Promise<void> {
		const tx = this.db.transaction('runs', 'readwrite');
		const store = tx.objectStore('runs');
		const rec = (await req(store.get(runId))) as RunRecord<J, Q> | undefined;
		if (rec) store.put({ ...rec, ...patch });
		await done(tx);
	}

	async activeRun(): Promise<StoredRun<A, J, Q> | null> {
		const tx = this.db.transaction(['meta', 'runs', 'actions'], 'readonly');
		const meta = (await req(tx.objectStore('meta').get('active'))) as { runId: string } | undefined;
		if (!meta) return null;
		const record = (await req(tx.objectStore('runs').get(meta.runId))) as RunRecord<J, Q> | undefined;
		if (!record) return null;
		const range = IDBKeyRange.bound([meta.runId, 0], [meta.runId, Number.MAX_SAFE_INTEGER]);
		const rows = (await req(tx.objectStore('actions').getAll(range))) as { seq: number; action: A }[];
		rows.sort((a, b) => a.seq - b.seq);
		// Only a gap-free prefix is trusted (an interrupted write never leaves holes, but be strict).
		const actions: A[] = [];
		for (let i = 0; i < rows.length && rows[i].seq === i; i++) actions.push(rows[i].action);
		return { record, actions };
	}

	async progress(): Promise<Progress> {
		const tx = this.db.transaction('meta', 'readonly');
		const row = (await req(tx.objectStore('meta').get('progress'))) as ({ key: string } & Partial<Progress>) | undefined;
		return {
			tutorialsSeen: row?.tutorialsSeen ?? [],
			advisorPoints: row?.advisorPoints ?? START_POINTS,
			settledRuns: row?.settledRuns ?? []
		};
	}

	async saveProgress(p: Progress): Promise<void> {
		const tx = this.db.transaction('meta', 'readwrite');
		tx.objectStore('meta').put({ key: 'progress', ...p });
		await done(tx);
	}

	/** Missions with at least one run played to the end (bankrupt runs do not count). */
	async finishedMissions(): Promise<string[]> {
		const tx = this.db.transaction('runs', 'readonly');
		const runs = (await req(tx.objectStore('runs').getAll())) as RunRecord<J, Q>[];
		return [...new Set(runs.filter((r) => r.status === 'finished').map((r) => r.missionId))];
	}

	/** Imports a run saved by the previous localStorage format (v1), then removes the legacy key. */
	async migrateLegacy(storage: Storage | undefined, policy: number): Promise<boolean> {
		const raw = storage?.getItem(LEGACY_KEY);
		if (!raw) return false;
		try {
			const s = JSON.parse(raw) as {
				missionId: string;
				seed: number;
				log: A[];
				journal?: J[];
				questions?: Q[];
				stopWarnings?: number;
				digest?: string;
			};
			const runId = newRunId();
			await this.createRun({
				runId,
				missionId: s.missionId,
				seed: s.seed,
				createdAt: Date.now(),
				executionPolicy: policy,
				journal: s.journal ?? [],
				questions: s.questions ?? [],
				stopWarnings: s.stopWarnings ?? 0,
				digest: s.digest ?? '',
				status: 'active',
				actions: 0
			});
			for (let i = 0; i < s.log.length; i++) await this.appendAction(runId, i, s.log[i]);
			storage?.removeItem(LEGACY_KEY);
			return true;
		} catch {
			return false;
		}
	}
}
