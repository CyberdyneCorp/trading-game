// Game session view-model (MVVM): owns the engine client, the action log and screen flow.
// Resume = deterministic replay of the logged actions (same mission, seed and commands =>
// same state digest), so only the log is persisted, never balances.

import { EventNames } from './eventNames.ts';
import { currentEnginePath, engineBuildLabel } from '#lib/engine/build.ts';
import { EngineClient } from '#lib/engine/client.ts';
import type { CsvBuildResult, EngineRequest } from '#lib/engine/protocol.ts';
import { DEFAULT_RUN_OPTIONS } from '#lib/engine/defaults.ts';
import type {
	CommandResult,
	CsvMeta,
	CsvValidation,
	DebriefData,
	GameState,
	LessonInfo,
	MissionInfo,
	MissionReveal,
	OrderSideName,
	OrderTypeName,
	OrderView,
	PackCheck,
	RunOptionsView,
	TrustedKey
} from '#lib/engine/types.ts';
import type { PlanEntry } from './analysis';
import { assistAllowed, rewindPrefix, type AssistedLink } from './assisted.ts';
import { stepsUntilNextQuestion } from './calibration.ts';
import {
	abandonCampaign,
	nextCampaignMission,
	settleCampaignRun,
	startCampaign,
	type CampaignLink,
	type LegResult
} from './campaign.ts';
import { legReport } from './campaignReport.ts';
import { CatalogModel, type CatalogHost, type CatalogStore } from './catalog.svelte.ts';
import { campaignPermissions } from './curriculum.ts';
import { i18n, t } from './i18n.svelte.ts';
import { adviceUsed, allowanceFor, settleRun } from './points.ts';
import { distinctFinished, newPerks, perkOptions, perksUsed, unlockedPerks, type PerkId } from './perks.ts';
import { randomSeedBytes, trustedKeys } from './packs.ts';
import { missingCapabilities, type MissingCapability } from './browserSupport.ts';
import { enginesToKeep, gcEngines, pinEngine } from './engineCache.ts';
import { formatBytes, MemoryPackIndex, opfsPackDir, PackLibrary, playableDigests, quotaShortfall, storageQuota, type PackEntry, type Quota } from './packStore.ts';
import { EMPTY_PROGRESS, newRunId, openDb, RunStore, type PackOrigin, type Progress, type RunRecord } from './storage.ts';
import { tutorialFor } from './tutorials.ts';
import { packOriginOf } from './missions.ts';

export type Screen =
	| 'loading'
	| 'menu'
	| 'import'
	| 'tutorial'
	| 'briefing'
	| 'terminal'
	| 'debrief'
	| 'curriculum'
	| 'journal'
	| 'lesson'
	| 'catalog';

type Action =
	| { k: 'lev'; lev: number }
	| {
			k: 'order';
			type: OrderTypeName;
			side: OrderSideName;
			qty: string;
			price: string | null;
			ro: boolean;
			stop?: string | null;
			tp?: string | null;
			trail?: string | null;
			trailRate?: string | null;
	  }
	| { k: 'opt'; series: number; side: OrderSideName; type: 'market' | 'limit'; qty: string; premium: string | null }
	| { k: 'margin'; amount: string }
	| {
			k: 'plan';
			side: OrderSideName;
			skipped: boolean;
			option: boolean;
			lose: boolean;
			inv: string | null;
			target: string | null;
			conf: number;
			risk: string | null;
	  }
	| { k: 'ans'; pBp: number; skipped: boolean }
	| { k: 'lesson'; q: number; choice: number; value: string | null; skipped: boolean }
	| { k: 'cancel'; id: number }
	| { k: 'move'; id: number; price: string }
	| { k: 'close' }
	| { k: 'advice' }
	| { k: 'adv'; n: number };

type CommandAction = Exclude<Action, { k: 'adv' }>;

/** A checkpoint every this many logged actions: resume restores it and replays at most this many. */
export const CHECKPOINT_EVERY = 20;

/** Absolute URL of an engine build path, for the worker's dynamic import. */
const engineUrl = (path: string) => new URL(path, globalThis.location?.href ?? 'http://localhost/').href;

/** Summary of the resumable run shown on the menu. */
export interface RunSummary {
	missionId: string;
	seed: number;
	actions: number;
	/** The campaign mission this run plays (null in free play and practice). */
	campaign: CampaignLink | null;
	/** The pack a practice run plays (null for a mission), so the menu can name it by its origin. */
	packDigest: string | null;
}

/** A run left behind by starting another one (it never reaches its debrief). */
interface AbandonedRun {
	runId: string;
	missionId: string;
	used: number;
	campaign: CampaignLink | null;
	equity: string;
}

/** Optional order extras: bracket exits (engine-side OCO) and trailing distance or rate (policy v16). */
export interface OrderExtras {
	stop?: string | null;
	takeProfit?: string | null;
	trail?: string | null;
	/** Trailing stop distance as a rate of the best close: "0.02" = 2%. */
	trailRate?: string | null;
}

export interface Notice {
	tone: 'ok' | 'warn' | 'error';
	text: string;
}

export class GameSession implements CatalogHost {
	screen = $state<Screen>('loading');
	error = $state<string | null>(null);
	missions = $state<MissionInfo[]>([]);
	missionId = $state('');
	seed = $state(0);
	// Raw, not a deep proxy: each engine reply replaces the whole state, and proxied reads of the
	// bar-wide indicator arrays made every chart redraw (zoom, hover) track thousands of signals.
	#state = $state.raw<GameState | null>(null);
	/** Order types and option contract names seen during this run, for the event feed. */
	readonly names = new EventNames();
	get state(): GameState | null {
		return this.#state;
	}
	set state(v: GameState | null) {
		this.#state = v;
		if (v) this.names.observe(v);
	}
	debrief = $state<DebriefData | null>(null);
	/** Seeded reveal of a built-in mission (regime spans, event outcomes); loaded only after the run ended. */
	reveal = $state<MissionReveal | null>(null);
	journal = $state<PlanEntry[]>([]);
	notice = $state<Notice | null>(null);
	stopWarnings = $state(0);
	/** A calibration question due now (engine schedule) blocks advancing until answered. */
	pendingQuestion = $derived(this.state?.calibration.due ?? false);
	busy = $state(false);
	saved = $state<RunSummary | null>(null);
	/** Mission ids played to the end at least once on this device (menu "Completed" mark). */
	completed = $state<string[]>([]);
	/** False when IndexedDB is unavailable (e.g. some private modes): runs cannot be resumed. */
	durable = $state(true);
	persisted = $state<boolean | null>(null);
	/** Per-device progress: tutorials already shown, advisor point balance. */
	progress = $state<Progress>({ ...EMPTY_PROGRESS });
	/** Menu-level problem (e.g. the saved run's pack was removed); cleared when a run starts. */
	menuAlert = $state<string | null>(null);
	/** Installed scenario packs (imported CSVs and loaded .tlpack files), verified by the engine; evicted ones flagged. */
	packs = $state<PackEntry[]>([]);
	/** Storage used and available for this origin (null when the browser does not say). */
	quota = $state<Quota | null>(null);
	/** Capabilities this browser lacks; non-empty = the unsupported-browser message instead of the game. */
	unsupported = $state<MissingCapability[]>([]);

	/** This device's signing identity for private imports (seed stays in IndexedDB). */
	private device: { seedHex: string; key: TrustedKey } | null = null;
	/** Start options of the current run (its advice allowance is the balance at start). */
	options = $state<RunOptionsView>({ ...DEFAULT_RUN_OPTIONS });
	/** The campaign mission the current run plays; null in free play and practice. */
	runCampaign = $state<CampaignLink | null>(null);
	/** Digest of the pack this run plays; null for a built-in mission. */
	runPack = $state<string | null>(null);
	/** Intel perks enabled for the current run (briefing toggles; empty outside free play). */
	perkChoice = $state<PerkId[]>([]);
	/** The mission's lesson questions (engine rubric, no answer key); null without any. */
	lessonInfo = $state<LessonInfo | null>(null);
	/** Lesson questions (rubric indices) already answered or skipped in this run. */
	lessonsAnswered = $state<number[]>([]);
	/** Assisted practice (rewind or another market): never counts for progress, points or the campaign. */
	runAssisted = $state<AssistedLink | null>(null);
	/** Run headers for the journal screen (loaded when it opens). */
	journalRecords = $state<RunRecord<PlanEntry, never>[]>([]);
	/** The Academy lesson open on the 'lesson' screen. */
	lessonId = $state('');
	/** Pack catalog and downloads (catalog screen). */
	readonly catalog: CatalogModel = new CatalogModel(this);

	private engine: EngineClient | null = null;
	/** This version's engine build (path) and the build loaded in the worker (differs while a pinned run plays). */
	private enginePath = '';
	private loadedEngine = '';
	private library: PackLibrary | null = null;
	private log: Action[] = [];
	private clientId = 1;
	private store: RunStore<Action, PlanEntry, never> | null = null;
	private runId = '';

	/** Identifies the current run (changes on a new mission; kept on resume). */
	get currentRunId(): string {
		return this.runId;
	}
	private policy = 0;

	async init(): Promise<void> {
		try {
			this.unsupported = await missingCapabilities();
			if (this.unsupported.length) return;
			this.engine = new EngineClient();
			this.enginePath = currentEnginePath();
			const info = await this.engine.request({ kind: 'init', engineUrl: engineUrl(this.enginePath) });
			this.loadedEngine = this.enginePath;
			this.missions = info.missions;
			this.policy = info.version.executionPolicy;
			await this.openStore();
			await this.refreshSaved();
			await this.refreshPacks();
			this.progress = (await this.store?.progress().catch(() => null)) ?? { ...EMPTY_PROGRESS };
			this.screen = 'menu';
		} catch (e) {
			this.error = e instanceof Error ? e.message : String(e);
		}
	}

	private async openStore(): Promise<void> {
		try {
			this.store = new RunStore(await openDb());
			await this.store.migrateLegacy(typeof localStorage === 'undefined' ? undefined : localStorage, this.policy);
		} catch {
			this.store = null;
			this.durable = false;
		}
		this.library = new PackLibrary(this.store ?? new MemoryPackIndex(), await opfsPackDir());
	}

	private async refreshSaved(): Promise<void> {
		const finished = await this.store?.finishedMissions().catch(() => []);
		if (finished) this.completed = [...new Set([...this.completed, ...finished])];
		const run = await this.store?.activeRun().catch(() => null);
		const active = run && run.record.status === 'active' ? run : null;
		const r = active?.record;
		this.saved = r && active ? { missionId: r.missionId, seed: r.seed, actions: active.actions.length, campaign: r.campaign ?? null, packDigest: r.packDigest ?? null } : null;
		// Only the resumable run needs its checkpoint and its engine build; the rest is reclaimed.
		await this.store?.pruneCheckpoints(r?.runId ?? null).catch(() => undefined);
		await gcEngines(enginesToKeep(this.enginePath, r?.engine));
	}

	/** Installed packs, each checked against its bytes (eviction), and the storage quota. */
	private async refreshPacks(): Promise<void> {
		this.packs = (await this.library?.list().catch(() => null)) ?? [];
		this.quota = await storageQuota();
	}

	/** Asks the browser to keep offline data (spec web-client "Persistent offline storage"). */
	private async requestPersistence(): Promise<void> {
		if (this.persisted !== null || typeof navigator === 'undefined' || !navigator.storage?.persist) return;
		this.persisted = await navigator.storage.persist().catch(() => false);
	}

	dispose(): void {
		this.engine?.dispose();
	}

	/** Optional fixed seed (from `?seed=` in the URL) for reproducible sessions and tests. */
	fixedSeed: number | null = null;

	private randomSeed(): number {
		return this.fixedSeed ?? Math.floor(Math.random() * 1_000_000_000);
	}

	/** Free play: any mission with its own rules (no campaign capital, no curriculum gate), with every unlocked perk on. */
	/** "Play again" from the debrief: the same pack on a new seed, or the mission on a new market. */
	async playAgain(): Promise<void> {
		if (this.runPack) await this.startPack(this.runPack);
		else await this.startMission(this.missionId);
	}

	async startMission(missionId: string, seed = this.randomSeed()): Promise<void> {
		await this.exclusive(async () => {
			await this.settleAbandoned();
			const perks = this.perksUnlocked;
			const options = { ...DEFAULT_RUN_OPTIONS, adviceAllowance: allowanceFor(this.progress), ...perkOptions(perks) };
			await this.launch(missionId, seed, options, null, perks);
		});
	}

	// ---- intel perks (spec progression-rewards "Intel perks unlocked by completed missions") ----

	/** Distinct built-in missions finished on this device (imported packs do not count). */
	get finishedCount(): number {
		return distinctFinished(this.completed, this.missions.map((m) => m.id));
	}

	get perksUnlocked(): PerkId[] {
		return unlockedPerks(this.finishedCount);
	}

	/** Perks apply to free play only: campaign missions and practice packs start without them. */
	get perksAllowed(): boolean {
		return !this.runCampaign && !this.state?.knownScenario;
	}

	/** Unlocked perks this briefing must explain before the player can use them. */
	get perksToIntroduce(): PerkId[] {
		return this.perksAllowed ? newPerks(this.perksUnlocked, this.progress.perksIntroduced) : [];
	}

	/**
	 * Briefing toggle. Perks are start options, so the run (no action yet) restarts on the same seed
	 * with the new options and its record follows; the market is the same with or without them.
	 */
	async setPerk(id: PerkId, on: boolean): Promise<void> {
		if (!this.perksAllowed || !this.perksUnlocked.includes(id) || this.log.some((a) => a.k !== 'lesson')) return;
		await this.exclusive(async () => {
			this.perkChoice = on ? [...new Set([...this.perkChoice, id])] : this.perkChoice.filter((p) => p !== id);
			this.options = { ...this.options, ...perkOptions(this.perkChoice) };
			this.state = await this.eng().request({ kind: 'start', missionId: this.missionId, seed: this.seed, options: { ...this.options } });
			// Lesson answers given on the briefing replay onto the restarted run with their client ids.
			this.clientId = 1;
			for (const a of this.log) await this.exec(a, false);
			await this.store?.updateRun(this.runId, { options: { ...this.options }, digest: this.state.digest }).catch(() => (this.durable = false));
		});
	}

	// ---- campaign runs (spec gameplay-loop "Game over on bankruptcy", spec curriculum) ----

	/** The mission the active campaign run plays next, or null. */
	get campaignMission(): string | null {
		return nextCampaignMission(this.progress.campaign);
	}

	/** Starts a new campaign run from the campaign's initial capital, then its first mission. */
	async startCampaign(seed = this.randomSeed()): Promise<void> {
		if (this.busy) return;
		this.saveProgress(startCampaign(this.progress, `camp-${newRunId()}`, Date.now()));
		await this.startCampaignMission(seed);
	}

	/** Resumes the campaign mission in progress, or starts the campaign's next mission. */
	async continueCampaign(): Promise<void> {
		const c = this.progress.campaign;
		const link = this.saved?.campaign;
		if (c && link && link.id === c.id && link.index === c.index) return this.resume();
		await this.startCampaignMission();
	}

	/** The active campaign run's next mission, from the carried capital and with the curriculum's gate. */
	async startCampaignMission(seed = this.randomSeed()): Promise<void> {
		await this.exclusive(async () => {
			await this.settleAbandoned(); // may move the campaign (a left mission carries its equity)
			const c = this.progress.campaign;
			const missionId = nextCampaignMission(c);
			if (!c || !missionId) return;
			const gate = campaignPermissions(missionId, this.progress.assessed).leverageCap;
			const options: RunOptionsView = {
				...DEFAULT_RUN_OPTIONS,
				adviceAllowance: allowanceFor(this.progress),
				startingBalance: c.capital,
				...(gate != null ? { leverageCap: gate } : {})
			};
			await this.launch(missionId, seed, options, { id: c.id, index: c.index });
		});
	}

	/**
	 * Ends the active campaign run; passed stages and every other progress stay. Its mission in
	 * progress is left with it (advice charged, no longer resumable) instead of resuming as a
	 * campaign mission of a campaign that is over.
	 */
	async endCampaign(): Promise<void> {
		await this.exclusive(async () => {
			const c = this.progress.campaign;
			if (c && this.saved?.campaign?.id === c.id) {
				await this.settleAbandoned();
				await this.store?.leaveActiveRun().catch(() => (this.durable = false));
				this.saved = null;
			}
			this.saveProgress(abandonCampaign(this.progress));
		});
	}

	// A new run always starts on this version's engine (a pinned older build serves only its own run).
	private async useCurrentEngine(): Promise<void> {
		if (this.loadedEngine !== this.enginePath) await this.switchEngine(this.enginePath);
	}

	private async switchEngine(path: string): Promise<void> {
		const v = await this.eng().request({ kind: 'useEngine', engineUrl: engineUrl(path) });
		this.loadedEngine = path;
		this.policy = v.executionPolicy;
	}

	/**
	 * An unfinished run resumes on the engine build it started on (spec web-client "Old run after
	 * update"). A run recorded before pinning, or whose build is gone, resumes on this version's
	 * build; the returned notice says so.
	 */
	private async useRunEngine(path: string | undefined): Promise<Notice | null> {
		const target = path || this.enginePath;
		if (target === this.loadedEngine) return null;
		try {
			await this.switchEngine(target);
			return target === this.enginePath ? null : { tone: 'ok', text: t('engine.pinned', { build: engineBuildLabel(target) }) };
		} catch {
			await this.switchEngine(this.enginePath);
			return { tone: 'warn', text: t('engine.pinMissing', { build: engineBuildLabel(target) }) };
		}
	}

	private async launch(
		missionId: string,
		seed: number,
		options: RunOptionsView,
		campaign: CampaignLink | null,
		perks: PerkId[] = [],
		assisted: AssistedLink | null = null
	): Promise<void> {
		await this.useCurrentEngine();
		this.resetRun(missionId, seed);
		this.options = options;
		this.perkChoice = perks;
		this.runCampaign = campaign;
		this.runAssisted = assisted;
		this.state = await this.eng().request({ kind: 'start', missionId, seed, options: { ...options } });
		this.lessonInfo = await this.eng().request({ kind: 'lesson', missionId });
		await this.createRunRecord(missionId, seed, { campaign: campaign ?? undefined, assisted: assisted ?? undefined });
		this.screen = this.needsTutorial(missionId) ? 'tutorial' : 'briefing';
	}

	// ---- assisted practice (spec gameplay-loop "Assisted practice", task 4.8) ----

	/** Rewind and another market are offered outside a campaign mission in progress. */
	get assistAllowed(): boolean {
		return !!this.state && assistAllowed(!!this.runCampaign, this.state.status);
	}

	/**
	 * Rewinds to the decision at `toStep` as a new attempt flagged assisted: the run is rebuilt from its
	 * own actions before that step (same market and start options, without the advisor), and the
	 * original attempt keeps its record, actions and score.
	 */
	async rewind(toStep: number): Promise<void> {
		const s = this.state;
		if (!s || !this.assistAllowed || toStep < s.warmupBars || toStep >= s.step) return;
		const prefix = rewindPrefix(this.log, s.warmupBars, toStep);
		const journal = this.journal.filter((p) => p.step < toStep);
		await this.exclusive(async () => {
			await this.settleAbandoned();
			const from = this.runId;
			const packDigest = s.knownScenario ? await this.packOfRun(from) : undefined;
			const options = { ...this.options, adviceAllowance: 0 };
			await this.startAssisted(this.missionId, this.seed, options, { from, kind: 'rewind', step: toStep }, packDigest);
			for (const a of prefix) await this.exec(a, true);
			this.lessonsAnswered = prefix.flatMap((a) => (a.k === 'lesson' ? [a.q] : []));
			this.journal = journal;
			this.persist();
			this.screen = 'terminal';
		});
	}

	/** The same mission on another market (a new seed), as an assisted attempt. */
	async practiceNewSeed(seed = this.randomSeed()): Promise<void> {
		const s = this.state;
		if (!s || !this.assistAllowed || s.knownScenario) return;
		await this.exclusive(async () => {
			await this.settleAbandoned();
			const options = { ...DEFAULT_RUN_OPTIONS, ...perkOptions(this.perkChoice) };
			await this.launch(this.missionId, seed, options, null, this.perkChoice, { from: this.runId, kind: 'seed' });
		});
	}

	private async startAssisted(missionId: string, seed: number, options: RunOptionsView, link: AssistedLink, packDigest?: string): Promise<void> {
		const perks = this.perkChoice;
		this.resetRun(missionId, seed);
		this.options = options;
		this.perkChoice = perks;
		await this.useCurrentEngine(); // a new attempt, so this version's engine (as for any new run)
		const state = await this.startEngine(missionId, seed, packDigest);
		if (!state) throw new Error('pack missing');
		// Flagged together with its first state: set before the (OPFS) pack read, the assisted tag showed
		// over the attempt being rewound.
		this.runAssisted = link;
		this.state = state;
		this.lessonInfo = packDigest ? null : await this.eng().request({ kind: 'lesson', missionId });
		await this.createRunRecord(missionId, seed, { packDigest, assisted: link });
	}

	// A pack run starts from its stored pack file; null when the pack is gone or was evicted.
	private async startEngine(missionId: string, seed: number, packDigest?: string): Promise<GameState | null> {
		if (!packDigest) return this.eng().request({ kind: 'start', missionId, seed, options: { ...this.options } });
		const pack = await this.packBytes(packDigest);
		if (!pack) return null;
		return this.eng().request({ kind: 'startPack', pack, keys: await this.trusted(), seed });
	}

	private async packOfRun(runId: string): Promise<string | undefined> {
		const runs = (await this.store?.runs().catch(() => null)) ?? [];
		return runs.find((r) => r.runId === runId)?.packDigest;
	}

	// ---- curriculum map and journal screens (web-client task 4.4) ----

	openCurriculum(): void {
		this.screen = 'curriculum';
	}

	// ---- Academy lessons (curriculum stages without a mission; web-client task 4.4) ----

	openLesson(id: string): void {
		this.lessonId = id;
		this.screen = 'lesson';
	}

	/**
	 * Records a completed lesson in local progress (passes its stage) and returns to the map once the
	 * write is committed, so a reload right after the map shows the stage passed cannot lose it.
	 */
	async finishLesson(id: string): Promise<void> {
		if (!this.progress.lessonsDone.includes(id)) {
			this.progress = { ...this.progress, lessonsDone: [...this.progress.lessonsDone, id] };
			await this.store?.saveProgress($state.snapshot(this.progress)).catch(() => (this.durable = false));
		}
		this.screen = 'curriculum';
	}

	// ---- pack catalog (web-client tasks 4.4, 5.3, 5.5) ----

	async openCatalog(): Promise<void> {
		this.screen = 'catalog';
		await this.catalog.load();
	}

	get installedDigests(): ReadonlySet<string> {
		return playableDigests(this.packs);
	}

	get catalogStore(): CatalogStore | null {
		return this.store;
	}

	online(): boolean {
		return typeof navigator === 'undefined' || navigator.onLine;
	}

	async openJournal(): Promise<void> {
		this.journalRecords = (await this.store?.runs().catch(() => null)) ?? [];
		this.screen = 'journal';
	}

	/** Practice run on an installed pack (e.g. an imported CSV); the pack is verified again on start. */
	async startPack(digest: string, seed = this.randomSeed()): Promise<void> {
		const pack = this.packs.find((p) => p.digest === digest);
		if (!pack || pack.evicted) return;
		await this.exclusive(async () => {
			const bytes = await this.packBytes(digest);
			if (!bytes) return this.markEvicted(digest); // evicted since the menu was drawn: no partial start
			await this.settleAbandoned();
			await this.useCurrentEngine();
			this.resetRun(pack.scenarioId, seed);
			this.runPack = digest;
			this.options = { ...DEFAULT_RUN_OPTIONS }; // pack runs start without advisor or perks
			this.state = await this.eng().request({ kind: 'startPack', pack: bytes, keys: await this.trusted(), seed });
			this.lessonInfo = null;
			await this.createRunRecord(pack.scenarioId, seed, { packDigest: digest });
			this.screen = 'briefing';
		});
	}

	private async createRunRecord(
		missionId: string,
		seed: number,
		link: { packDigest?: string; campaign?: CampaignLink; assisted?: AssistedLink } = {}
	): Promise<void> {
		this.runId = newRunId();
		await this.store
			?.createRun({
				runId: this.runId,
				missionId,
				seed,
				createdAt: Date.now(),
				executionPolicy: this.policy,
				journal: [],
				questions: [],
				stopWarnings: 0,
				digest: this.state?.digest ?? '',
				status: 'active',
				actions: 0,
				options: { ...this.options },
				equity: this.state?.account.equity,
				engine: this.loadedEngine,
				...(link.packDigest ? { packDigest: link.packDigest } : {}),
				...(link.campaign ? { campaign: { ...link.campaign } } : {}),
				...(link.assisted ? { assisted: { ...link.assisted } } : {})
			})
			.catch(() => (this.durable = false));
		void this.requestPersistence();
		void pinEngine(this.loadedEngine); // kept across app updates until the run ends
	}

	// ---- scenario packs and CSV import (spec scenario-packs; web-client task 4.5) ----

	openImport(): void {
		this.screen = 'import';
	}

	private async deviceIdentity(): Promise<{ seedHex: string; key: TrustedKey }> {
		if (this.device) return this.device;
		const fresh = () => [...randomSeedBytes()].map((b) => b.toString(16).padStart(2, '0')).join('');
		const seedHex = (await this.store?.deviceSeed(randomSeedBytes).catch(() => null)) ?? fresh();
		this.device = { seedHex, key: await this.eng().request({ kind: 'deviceKey', seedHex }) };
		return this.device;
	}

	/** Keys a pack may be signed with: pinned publishers plus this device (its own imports). */
	private async trusted(): Promise<TrustedKey[]> {
		return trustedKeys((await this.deviceIdentity()).key);
	}

	async validateCsv(csv: string, meta: CsvMeta): Promise<CsvValidation> {
		return this.eng().request({ kind: 'csvValidate', csv, meta: { ...meta } });
	}

	/** CSV -> private pack signed by this device, then verified and installed like any pack. */
	async importCsv(csv: string, meta: CsvMeta, label: string): Promise<CsvBuildResult> {
		const { seedHex } = await this.deviceIdentity();
		const res = await this.eng().request({ kind: 'csvBuild', csv, meta: { ...meta }, seedHex });
		if (!res.ok) return res;
		const check = await this.installPack(res.pack, label);
		return check.ok ? res : { ...res, ok: false, reason: check.reason };
	}

	/** Where the current run's pack came from (null for a mission run). */
	get runPackOrigin(): PackOrigin | null {
		return packOriginOf(this.packs, this.runPack);
	}

	/**
	 * Verify-then-install: only bytes the engine accepted are stored (by digest), and only when they
	 * fit in the storage quota (spec web-client "Quota exceeded": the space missing is reported).
	 */
	async installPack(bytes: Uint8Array, label: string, origin: PackOrigin = 'import'): Promise<PackCheck> {
		this.quota = await storageQuota();
		const short = quotaShortfall(this.quota, bytes.length);
		if (short > 0) return { ok: false, reason: `quota:${formatBytes(short, i18n.locale)}`, detail: '' };
		const check = await this.eng().request({ kind: 'packValidate', pack: bytes, keys: await this.trusted() });
		if (!check.ok || !check.pack || !this.library) return check;
		const s = check.pack;
		const meta = {
			digest: s.digest,
			scenarioId: s.scenarioId,
			mode: s.mode,
			label,
			private: s.private,
			totalBars: s.totalBars,
			warmupBars: s.warmupBars,
			leverageCap: s.leverageCap,
			gaps: s.gaps,
			addedAt: Date.now(),
			origin
		};
		const entry = await this.library.install(meta, bytes).catch(() => null);
		if (!entry) {
			this.durable = false;
			return { ok: false, reason: 'storage_failed', detail: '' };
		}
		this.packs = [...this.packs.filter((p) => p.digest !== entry.digest), entry];
		this.quota = await storageQuota();
		return check;
	}

	async removePack(digest: string): Promise<void> {
		await this.library?.remove(digest).catch(() => (this.durable = false));
		this.packs = this.packs.filter((p) => p.digest !== digest);
		this.quota = await storageQuota();
	}

	/** The pack file (to start, resume or export it); null when it was evicted. */
	async packBytes(digest: string): Promise<Uint8Array | null> {
		return (await this.library?.bytes(digest).catch(() => null)) ?? null;
	}

	private markEvicted(digest: string): void {
		this.packs = this.packs.map((p) => (p.digest === digest ? { ...p, evicted: true } : p));
		this.menuAlert = t('pack.evicted');
	}

	// Start/resume/commands share one engine run: never let two of them interleave
	// (e.g. Space re-activating a still-focused "Resume" button during replay).
	private async exclusive(fn: () => Promise<void>): Promise<void> {
		if (this.busy) return;
		this.busy = true;
		try {
			await fn();
		} finally {
			this.busy = false;
		}
	}

	async resume(): Promise<void> {
		await this.exclusive(() => this.replaySaved());
	}

	private async replaySaved(): Promise<void> {
		const run = await this.store?.activeRun();
		if (!run) return;
		const r = run.record;
		// A pack run needs its pack's bytes; an evicted pack never starts from partial data.
		const pack = r.packDigest ? await this.packBytes(r.packDigest) : null;
		if (r.packDigest && !pack) {
			this.menuAlert = t('pack.missing');
			this.saved = null;
			return;
		}
		this.options = { ...DEFAULT_RUN_OPTIONS, ...r.options };
		const engineNotice = await this.useRunEngine(r.engine);
		this.resetRun(r.missionId, r.seed);
		this.runId = r.runId;
		this.runCampaign = r.campaign ?? null;
		this.runAssisted = r.assisted ?? null;
		this.lessonInfo = r.packDigest ? null : await this.eng().request({ kind: 'lesson', missionId: r.missionId });
		this.lessonsAnswered = run.actions.flatMap((a) => (a.k === 'lesson' ? [a.q] : []));
		this.runPack = r.packDigest ?? null;
		this.perkChoice = perksUsed(this.options);
		const restored = await this.restoreOrStart(r, run.actions, pack);
		for (const a of run.actions.slice(restored)) await this.exec(a, false);
		// Keep the replayed history so the next action gets the right sequence number.
		this.log = [...run.actions];
		this.journal = r.journal;
		this.stopWarnings = r.stopWarnings;
		this.notice = engineNotice ?? this.replayNotice(r, run.actions.length);
		this.screen = this.state?.status === 'active' ? 'terminal' : 'debrief';
		if (this.screen === 'debrief') await this.loadDebrief();
		this.saved = null;
	}

	private replayNotice(r: RunRecord<PlanEntry, never>, actions: number): Notice | null {
		if (r.executionPolicy !== this.policy) {
			return { tone: 'warn', text: `run recorded with execution policy v${r.executionPolicy}; engine is v${this.policy}` };
		}
		if (this.state && r.digest && r.actions === actions && this.state.digest !== r.digest) {
			return { tone: 'warn', text: `replay digest mismatch (${r.digest.slice(0, 8)})` };
		}
		return null;
	}

	/**
	 * Puts the engine at the run's latest checkpoint when there is a usable one (written by the same
	 * engine build, restored digest as recorded), else at the run's start. Returns how many logged
	 * actions the engine state already includes; the caller replays the rest.
	 */
	private async restoreOrStart(r: RunRecord<PlanEntry, never>, actions: Action[], pack: Uint8Array | null): Promise<number> {
		const keys = pack ? await this.trusted() : [];
		const cp = await this.store?.checkpoint(r.runId).catch(() => null);
		if (cp && cp.engine === this.loadedEngine && cp.actions <= actions.length) {
			const source = pack ? { pack, keys } : {};
			const state = await this.eng()
				.request({ kind: 'restore', missionId: r.missionId, seed: r.seed, checkpoint: cp.hex, ...source })
				.catch(() => null);
			if (state && state.digest === cp.digest) {
				this.state = state;
				this.names.restore(cp.seriesNames ?? []);
				this.clientId = 1 + actions.slice(0, cp.actions).filter((a) => a.k !== 'adv').length;
				return cp.actions;
			}
		}
		this.state = pack
			? await this.eng().request({ kind: 'startPack', pack, keys, seed: r.seed })
			: await this.eng().request({ kind: 'start', missionId: r.missionId, seed: r.seed, options: { ...this.options } });
		return 0;
	}

	// Every CHECKPOINT_EVERY logged actions: the engine's state after them, for a short resume. The
	// worker answers in request order, so the checkpoint is the state right after this action.
	private checkpoint(): void {
		const store = this.store;
		const actions = this.log.length;
		if (!store || !this.state || actions % CHECKPOINT_EVERY !== 0) return;
		const base = { runId: this.runId, actions, digest: this.state.digest, engine: this.loadedEngine, seriesNames: this.names.entries() };
		void this.eng()
			.request({ kind: 'checkpoint' })
			.then((cp) => store.saveCheckpoint({ ...base, hex: cp.checkpoint }))
			.catch(() => undefined); // only an optimization: resume replays the whole log without it
	}

	/** A mission's tutorial is shown automatically only the first time it is started on this device. */
	needsTutorial(missionId: string): boolean {
		return tutorialFor(missionId, 'en').length > 0 && !this.progress.tutorialsSeen.includes(missionId);
	}

	/** Replays the current mission's tutorial (from the briefing). */
	showTutorial(): void {
		this.screen = 'tutorial';
	}

	/** Finished or skipped: remember it and go on to the briefing. */
	finishTutorial(): void {
		if (!this.progress.tutorialsSeen.includes(this.missionId)) {
			this.progress = { ...this.progress, tutorialsSeen: [...this.progress.tutorialsSeen, this.missionId] };
			void this.store?.saveProgress($state.snapshot(this.progress)).catch(() => (this.durable = false));
		}
		this.screen = 'briefing';
	}

	/** Leaves the briefing for the terminal; the perks it explained are remembered as introduced. */
	enterTerminal(): void {
		const shown = this.perksToIntroduce;
		if (shown.length) this.saveProgress({ ...this.progress, perksIntroduced: [...this.progress.perksIntroduced, ...shown] });
		this.screen = 'terminal';
	}

	backToMenu(): void {
		this.screen = 'menu';
		void this.refreshSaved();
		void this.refreshPacks();
	}

	async setLeverage(lev: number): Promise<CommandResult | null> {
		return this.command({ k: 'lev', lev });
	}

	async placeOrder(
		type: OrderTypeName,
		side: OrderSideName,
		qty: string,
		price: string | null,
		reduceOnly = false,
		extras: OrderExtras = {}
	): Promise<CommandResult | null> {
		const res = await this.command({
			k: 'order',
			type,
			side,
			qty,
			price,
			ro: reduceOnly,
			stop: extras.stop ?? null,
			tp: extras.takeProfit ?? null,
			trail: extras.trail ?? null,
			...(extras.trailRate ? { trailRate: extras.trailRate } : {})
		});
		if (res?.warning === 'stop_beyond_liquidation') this.stopWarnings += 1;
		return res;
	}

	async placeOptionOrder(
		series: number,
		side: OrderSideName,
		type: 'market' | 'limit',
		qty: string,
		premium: string | null
	): Promise<CommandResult | null> {
		return this.command({ k: 'opt', series, side, type, qty, premium });
	}

	/** Adds (positive) or removes (negative) isolated margin; amount is a decimal string. */
	async adjustMargin(amount: string): Promise<CommandResult | null> {
		return this.command({ k: 'margin', amount });
	}

	async cancel(orderId: number): Promise<CommandResult | null> {
		return this.command({ k: 'cancel', id: orderId });
	}

	/**
	 * Moves a working stop or take-profit in place (engine MoveOrder, policy v13): same order id,
	 * quantity and OCO link, so a moved bracket leg still cancels its sibling. A rejected move leaves
	 * the order where it was.
	 */
	async moveOrder(o: OrderView, price: string): Promise<CommandResult | null> {
		const res = await this.command({ k: 'move', id: o.id, price });
		if (res?.warning === 'stop_beyond_liquidation') this.stopWarnings += 1;
		return res;
	}

	async closePosition(): Promise<CommandResult | null> {
		return this.command({ k: 'close' });
	}

	/** Asks the advisor (one point of this run's allowance); the advice arrives in state.advisor. */
	async requestAdvice(): Promise<CommandResult | null> {
		if (!this.state || this.state.advisor.left <= 0) return null;
		return this.command({ k: 'advice' });
	}

	/** Calibration answer (probability the close in `horizon` bars ends above the threshold). */
	async answerQuestion(p: number | null): Promise<void> {
		if (!this.pendingQuestion) return;
		await this.command({ k: 'ans', pBp: p == null ? 0 : Math.round(p * 10000), skipped: p == null });
		this.flushQueue();
	}

	/**
	 * A lesson question (rubric index) answered on the briefing: a choice index, a numeric value as a
	 * decimal string, or null to skip it. Recorded once by the engine (understanding dimension).
	 */
	async answerLesson(question: number, answer: { choice?: number; value?: string } | null): Promise<CommandResult | null> {
		if (this.lessonsAnswered.includes(question)) return null;
		const res = await this.command({
			k: 'lesson',
			q: question,
			choice: answer?.choice ?? 0,
			value: answer?.value ?? null,
			skipped: answer == null
		});
		if (res?.accepted) this.lessonsAnswered = [...this.lessonsAnswered, question];
		return res;
	}

	/** Structured plan goes to the engine (scored there); the free text stays in the local journal. */
	async recordPlan(plan: PlanEntry): Promise<void> {
		this.journal = [...this.journal, plan];
		this.persist();
		const price = (v: number | null) => (v != null && v > 0 ? v.toFixed(2) : null);
		await this.command({
			k: 'plan',
			side: plan.side,
			skipped: plan.skipped,
			option: plan.instrument != null,
			lose: !!plan.loseScenario,
			inv: price(plan.invalidation),
			target: price(plan.target),
			conf: plan.confidence,
			risk: plan.plannedRisk != null && plan.plannedRisk > 0 ? plan.plannedRisk.toFixed(2) : null
		});
	}

	async advance(n: number): Promise<void> {
		if (!this.state || this.state.status !== 'active' || this.pendingQuestion) return;
		if (this.busy) {
			this.queued += n; // e.g. Space pressed while an order is in flight: run it afterwards
			return;
		}
		const steps = this.state ? stepsUntilNextQuestion(this.state, n) : n;
		this.busy = true;
		try {
			await this.exec({ k: 'adv', n: steps }, true);
			this.persist();
			if (this.state && this.state.status !== 'active') {
				this.markCompleted();
				await this.loadDebrief();
				this.screen = 'debrief';
			}
		} finally {
			this.busy = false;
		}
		this.flushQueue();
	}

	private queued = 0;
	private hold = 0;

	/**
	 * Runs several commands as one player intent (e.g. plan + order). Queued advances wait until the
	 * whole sequence is done, so an advance can never slip between the plan and its order.
	 */
	async sequence(fn: () => Promise<void>): Promise<void> {
		this.hold += 1;
		try {
			await fn();
		} finally {
			this.hold -= 1;
			this.flushQueue();
		}
	}

	private flushQueue(): void {
		if (this.queued <= 0 || this.busy || this.hold > 0 || this.pendingQuestion) return;
		const n = this.queued;
		this.queued = 0;
		void this.advance(n);
	}

	private resetRun(missionId: string, seed: number): void {
		this.menuAlert = null;
		this.missionId = missionId;
		this.seed = seed;
		this.log = [];
		this.clientId = 1;
		this.journal = [];
		this.notice = null;
		this.debrief = null;
		this.reveal = null;
		this.stopWarnings = 0;
		this.queued = 0;
		this.runCampaign = null;
		this.runAssisted = null;
		this.lessonsAnswered = [];
		this.runPack = null;
		this.perkChoice = [];
		this.names.reset();
	}

	private eng(): EngineClient {
		if (!this.engine) throw new Error('engine not initialized');
		return this.engine;
	}

	private async command(a: Action): Promise<CommandResult | null> {
		if (!this.state || this.state.status !== 'active' || this.busy) return null;
		this.busy = true;
		try {
			const res = await this.exec(a, true);
			this.persist();
			return res;
		} finally {
			this.busy = false;
			queueMicrotask(() => this.flushQueue());
		}
	}

	// Runs one action against the engine. `live` = user action (logged); replay passes false.
	private async exec(a: Action, live: boolean): Promise<CommandResult | null> {
		if (live) {
			// Commit before the engine sees it: an action shown as accepted is never lost.
			await this.store?.appendAction(this.runId, this.log.length, a).catch(() => (this.durable = false));
			this.log.push(a);
		}
		const e = this.eng();
		let result: CommandResult | null = null;
		if (a.k === 'adv') {
			this.state = await e.request({ kind: 'advance', steps: a.n });
		} else {
			const r = await e.request(commandRequest(a, this.clientId++));
			this.state = r.state;
			result = r.result;
		}
		if (live) this.checkpoint();
		return result;
	}

	// In memory too: the menu must not wait for the fire-and-forget header write in persist().
	// A practice run on an imported pack is not a mission, so it never counts as completed.
	private markCompleted(): void {
		if (this.runAssisted) return; // assisted practice never counts as completing a mission
		if (this.state?.status === 'finished' && !this.state.knownScenario && !this.completed.includes(this.missionId)) {
			this.completed = [...this.completed, this.missionId];
		}
	}

	private async loadDebrief(): Promise<void> {
		this.debrief = await this.eng().request({ kind: 'debrief' });
		this.reveal = await this.eng().request({ kind: 'reveal' });
		this.settleEnded();
	}

	// Spec progression-rewards "Local point balance": balance - used + earned, once per run id.
	// Practice on a known scenario (imported pack) starts without advice and earns nothing.
	// A campaign mission carries the engine's final equity on (or ends the campaign run when bankrupt).
	private settleEnded(): void {
		const s = this.state;
		const d = this.debrief;
		// Assisted practice settles nothing: no points, no campaign leg (it starts without the advisor).
		if (!s || !d || s.status === 'active' || s.knownScenario || this.runAssisted) return;
		let next = settleRun(this.progress, this.runId, s.advisor.advice.length, d.advisorPointsEarned);
		if (this.runCampaign) {
			const result: LegResult = s.status === 'bankrupt' ? 'bankrupt' : 'finished';
			const report = legReport(d, s.totals);
			next = settleCampaignRun(next, this.runCampaign, { runId: this.runId, missionId: this.missionId, result, equity: d.finalEquity, report });
		}
		this.saveProgress(next);
	}

	// Starting a run replaces the resumable one, which never reaches its debrief: charge the advice it
	// used now (it earns nothing), or leaving a run would give its reads back for free. A left campaign
	// mission carries the equity it had, and the campaign stays on that mission.
	private async settleAbandoned(): Promise<void> {
		const run = await this.abandonedRun();
		if (!run) return;
		let next = settleRun(this.progress, run.runId, run.used, 0);
		if (run.campaign) {
			next = settleCampaignRun(next, run.campaign, { runId: run.runId, missionId: run.missionId, result: 'left', equity: run.equity });
		}
		this.saveProgress(next);
	}

	// The stored active run (also after a reload), else the one in memory when nothing is stored.
	private async abandonedRun(): Promise<AbandonedRun | null> {
		const stored = await this.store?.activeRun().catch(() => null);
		if (stored) {
			const r = stored.record;
			if (r.status !== 'active' || r.packDigest || r.assisted) return null;
			const equity = r.equity ?? r.options?.startingBalance ?? '0';
			return { runId: r.runId, missionId: r.missionId, used: adviceUsed(stored.actions), campaign: r.campaign ?? null, equity };
		}
		const s = this.state;
		if (!s || s.status !== 'active' || s.knownScenario || !this.runId || this.runAssisted) return null;
		return { runId: this.runId, missionId: this.missionId, used: s.advisor.advice.length, campaign: this.runCampaign, equity: s.account.equity };
	}

	private saveProgress(next: Progress): void {
		if (next === this.progress) return;
		this.progress = next;
		void this.store?.saveProgress($state.snapshot(this.progress)).catch(() => (this.durable = false));
	}

	// Header fields only (journal, questions, digest, status); actions are committed in exec().
	private persist(): void {
		if (!this.state || !this.store || !this.runId) return;
		void this.store
			.updateRun(this.runId, {
				journal: $state.snapshot(this.journal),
				stopWarnings: this.stopWarnings,
				digest: this.state.digest,
				status: this.state.status,
				equity: this.state.account.equity
			})
			.catch(() => (this.durable = false));
	}
}

// The engine request for one logged command (client ids are assigned in log order).
function commandRequest(a: CommandAction, clientId: number): Extract<EngineRequest, { clientId: number }> {
	switch (a.k) {
		case 'lev':
			return { kind: 'leverage', clientId, leverage: a.lev };
		case 'order':
			return {
				kind: 'order',
				clientId,
				type: a.type,
				side: a.side,
				qty: a.qty,
				price: a.price,
				reduceOnly: a.ro,
				attachStop: a.stop ?? null,
				attachTp: a.tp ?? null,
				trailDistance: a.trail ?? null,
				trailRate: a.trailRate ?? null
			};
		case 'opt':
			return { kind: 'option', clientId, series: a.series, side: a.side, type: a.type, qty: a.qty, premium: a.premium };
		case 'plan':
			return {
				kind: 'plan',
				clientId,
				side: a.side,
				skipped: a.skipped,
				option: a.option,
				loseScenario: a.lose,
				invalidation: a.inv,
				target: a.target,
				confidence: a.conf,
				plannedRisk: a.risk
			};
		case 'ans':
			return { kind: 'answer', clientId, pBp: a.pBp, skipped: a.skipped };
		case 'lesson':
			return { kind: 'lessonAnswer', clientId, question: a.q, choice: a.choice, value: a.value, skipped: a.skipped };
		case 'margin':
			return { kind: 'margin', clientId, amount: a.amount };
		case 'cancel':
			return { kind: 'cancel', clientId, orderId: a.id };
		case 'move':
			return { kind: 'move', clientId, orderId: a.id, price: a.price };
		case 'close':
			return { kind: 'close', clientId };
		case 'advice':
			return { kind: 'advice', clientId };
	}
}
