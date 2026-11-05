// IndexedDB persistence for runs (spec web-client "Local persistence", offline-sync "Durable decisions").
//
// A run is a header (mission, seed, policy, journal, ...) plus an append-only action log keyed by
// (runId, seq). Each action is committed in its own transaction BEFORE it is sent to the engine,
// so anything the UI showed as accepted survives a crash or a closed tab. Resume replays the log
// through the deterministic engine; the stored digest only verifies the replay.

import type { AssistedLink } from './assisted.ts';
import type { CampaignLink, CampaignProgress, CampaignRun } from './campaign.ts';
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
	/** 'left': replaced by another run (or its campaign ended) before the end; it cannot be resumed. */
	status: 'active' | 'finished' | 'bankrupt' | 'left';
	actions: number; // number of committed actions
	/**
	 * Start options (advice allowance, perks; campaign capital and leverage cap since policy v12);
	 * absent on runs recorded before policy v11 = defaults.
	 */
	options?: { adviceAllowance: number; newsLead: number; calendar: boolean; startingBalance?: string; leverageCap?: number };
	/** Runs on a scenario pack (e.g. an imported CSV): the pack's digest in the `packs` store. */
	packDigest?: string;
	/** Campaign runs: the campaign run and mission index this run plays. */
	campaign?: CampaignLink;
	/** Last known engine equity (decimal string): what a campaign mission carries when it is left. */
	equity?: string;
	/** Assisted practice (rewind or another market, task 4.8): excluded from progress, points and the campaign. */
	assisted?: AssistedLink;
}

/**
 * An installed pack, stored by digest after the engine verified it (spec web-client "Persistent
 * offline storage"; IndexedDB stands in for OPFS). `label` is the player's own name for it.
 */
export interface StoredPack {
	digest: string;
	scenarioId: string;
	label: string;
	bytes: Uint8Array;
	private: boolean;
	totalBars: number;
	warmupBars: number;
	leverageCap: number;
	gaps: number;
	addedAt: number;
}

export interface StoredRun<A, J, Q> {
	record: RunRecord<J, Q>;
	actions: A[];
}

/**
 * Per-device player progress (specs mission-tutorial, progression-rewards, curriculum). Version 2
 * adds the campaign run, its history and the missions assessed in a campaign (curriculum stages);
 * `perksIntroduced` is a later key of version 2 (absent = none explained yet).
 */
export interface Progress extends CampaignProgress {
	version: 2;
	tutorialsSeen: string[];
	/** Advisor point balance (see points.ts). */
	advisorPoints: number;
	/** Runs whose points were already settled (idempotency). */
	settledRuns: string[];
	/** Intel perks already explained on a briefing (perks.ts). */
	perksIntroduced: string[];
}

export const EMPTY_PROGRESS: Progress = {
	version: 2,
	tutorialsSeen: [],
	advisorPoints: START_POINTS,
	settledRuns: [],
	perksIntroduced: [],
	campaign: null,
	campaignHistory: [],
	assessed: []
};

const strings = (v: unknown): string[] => (Array.isArray(v) ? v.filter((x): x is string => typeof x === 'string') : []);
const isCampaign = (v: unknown): v is CampaignRun =>
	!!v && typeof v === 'object' && typeof (v as CampaignRun).id === 'string' && Array.isArray((v as CampaignRun).legs);

/**
 * Reads any stored progress shape: version 1 (tutorials, then advisor points) gets the version 2
 * fields empty; unknown or damaged fields fall back to their defaults instead of failing.
 */
export function migrateProgress(row: Record<string, unknown> | undefined): Progress {
	const r = row ?? {};
	const history = Array.isArray(r.campaignHistory) ? r.campaignHistory.filter(isCampaign) : [];
	return {
		version: 2,
		tutorialsSeen: strings(r.tutorialsSeen),
		advisorPoints: typeof r.advisorPoints === 'number' ? r.advisorPoints : START_POINTS,
		settledRuns: strings(r.settledRuns),
		perksIntroduced: strings(r.perksIntroduced),
		campaign: isCampaign(r.campaign) ? r.campaign : null,
		campaignHistory: history,
		assessed: strings(r.assessed)
	};
}

const DB_NAME = 'tradelab';
const DB_VERSION = 2; // v2: packs store
const LEGACY_KEY = 'tradelab.run.v1';

function req<T>(r: IDBRequest<T>): Promise<T> {
	return new Promise((resolve, reject) => {
		r.onsuccess = () => resolve(r.result);
		r.onerror = () => reject(r.error);
	});
}

// Within `tx` (runs + meta, readwrite): the active run, if still unfinished, becomes 'left'.
async function leaveActive(tx: IDBTransaction): Promise<void> {
	const meta = (await req(tx.objectStore('meta').get('active'))) as { runId: string } | undefined;
	if (!meta) return;
	const runs = tx.objectStore('runs');
	const rec = (await req(runs.get(meta.runId))) as RunRecord | undefined;
	if (rec?.status === 'active') runs.put({ ...rec, status: 'left' });
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
			if (!db.objectStoreNames.contains('packs')) db.createObjectStore('packs', { keyPath: 'digest' });
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

	/** Stores a new run and makes it the active one; the run it replaces, if unfinished, is left. */
	async createRun(record: RunRecord<J, Q>): Promise<void> {
		const tx = this.db.transaction(['runs', 'meta'], 'readwrite');
		await leaveActive(tx);
		tx.objectStore('runs').put(record);
		tx.objectStore('meta').put({ key: 'active', runId: record.runId });
		await done(tx);
	}

	/** Marks the active run as left (no longer resumable); finished runs are kept as they are. */
	async leaveActiveRun(): Promise<void> {
		const tx = this.db.transaction(['runs', 'meta'], 'readwrite');
		await leaveActive(tx);
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
		const row = (await req(tx.objectStore('meta').get('progress'))) as Record<string, unknown> | undefined;
		return migrateProgress(row);
	}

	async saveProgress(p: Progress): Promise<void> {
		const tx = this.db.transaction('meta', 'readwrite');
		tx.objectStore('meta').put({ key: 'progress', ...p });
		await done(tx);
	}

	async savePack(p: StoredPack): Promise<void> {
		const tx = this.db.transaction('packs', 'readwrite');
		tx.objectStore('packs').put(p);
		await done(tx);
	}

	async pack(digest: string): Promise<StoredPack | null> {
		const tx = this.db.transaction('packs', 'readonly');
		return ((await req(tx.objectStore('packs').get(digest))) as StoredPack | undefined) ?? null;
	}

	/** Installed packs, oldest first. */
	async packs(): Promise<StoredPack[]> {
		const tx = this.db.transaction('packs', 'readonly');
		const all = (await req(tx.objectStore('packs').getAll())) as StoredPack[];
		return all.sort((a, b) => a.addedAt - b.addedAt);
	}

	async deletePack(digest: string): Promise<void> {
		const tx = this.db.transaction('packs', 'readwrite');
		tx.objectStore('packs').delete(digest);
		await done(tx);
	}

	/**
	 * This device's signing seed for private imports (spec design D1), created on first use from
	 * `random`. It never leaves the device; only its public key is used to verify.
	 */
	async deviceSeed(random: () => Uint8Array): Promise<string> {
		const tx = this.db.transaction('meta', 'readwrite');
		const store = tx.objectStore('meta');
		const row = (await req(store.get('deviceSeed'))) as { key: string; hex: string } | undefined;
		const hex = row?.hex ?? [...random()].map((b) => b.toString(16).padStart(2, '0')).join('');
		if (!row) store.put({ key: 'deviceSeed', hex });
		await done(tx);
		return hex;
	}

	/**
	 * Every run header (no action logs), newest first: the journal screen reads them. An 'active' run
	 * that is not the resumable one was replaced before runs were marked left: it reads as left.
	 */
	async runs(): Promise<RunRecord<J, Q>[]> {
		const tx = this.db.transaction(['runs', 'meta'], 'readonly');
		const meta = (await req(tx.objectStore('meta').get('active'))) as { runId: string } | undefined;
		const all = (await req(tx.objectStore('runs').getAll())) as RunRecord<J, Q>[];
		const stale = (r: RunRecord<J, Q>) => r.status === 'active' && r.runId !== meta?.runId;
		return all.map((r) => (stale(r) ? { ...r, status: 'left' as const } : r)).sort((a, b) => b.createdAt - a.createdAt);
	}

	/** Missions with at least one run played to the end (bankrupt runs do not count). */
	async finishedMissions(): Promise<string[]> {
		const tx = this.db.transaction('runs', 'readonly');
		const runs = (await req(tx.objectStore('runs').getAll())) as RunRecord<J, Q>[];
		// Practice runs on an imported pack are not missions, and assisted attempts do not count either.
		return [...new Set(runs.filter((r) => r.status === 'finished' && !r.packDigest && !r.assisted).map((r) => r.missionId))];
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
