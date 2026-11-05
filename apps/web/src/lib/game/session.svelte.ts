// Game session view-model (MVVM): owns the engine client, the action log and screen flow.
// Resume = deterministic replay of the logged actions (same mission, seed and commands =>
// same state digest), so only the log is persisted, never balances.

import { EventNames } from './eventNames.ts';
import { EngineClient } from '#lib/engine/client.ts';
import type { CsvBuildResult, EngineRequest } from '#lib/engine/protocol.ts';
import {
	DEFAULT_RUN_OPTIONS,
	type CommandResult,
	type CsvMeta,
	type CsvReport,
	type DebriefData,
	type GameState,
	type MissionInfo,
	type MissionReveal,
	type OrderSideName,
	type OrderTypeName,
	type OrderView,
	type PackCheck,
	type RunOptionsView,
	type TrustedKey
} from '#lib/engine/types.ts';
import type { PlanEntry } from './analysis';
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
import { campaignPermissions } from './curriculum.ts';
import { t } from './i18n.svelte.ts';
import { adviceUsed, allowanceFor, settleRun } from './points.ts';
import { distinctFinished, newPerks, perkOptions, perksUsed, unlockedPerks, type PerkId } from './perks.ts';
import { randomSeedBytes, trustedKeys } from './packs.ts';
import { EMPTY_PROGRESS, newRunId, openDb, RunStore, type Progress, type RunRecord, type StoredPack } from './storage.ts';
import { tutorialFor } from './tutorials.ts';

export type Screen = 'loading' | 'menu' | 'import' | 'tutorial' | 'briefing' | 'terminal' | 'debrief' | 'curriculum' | 'journal';

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
	| { k: 'cancel'; id: number }
	| { k: 'move'; id: number; price: string }
	| { k: 'close' }
	| { k: 'advice' }
	| { k: 'adv'; n: number };

type CommandAction = Exclude<Action, { k: 'adv' }>;

/** Summary of the resumable run shown on the menu. */
export interface RunSummary {
	missionId: string;
	seed: number;
	actions: number;
	/** The campaign mission this run plays (null in free play and practice). */
	campaign: CampaignLink | null;
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

export class GameSession {
	screen = $state<Screen>('loading');
	error = $state<string | null>(null);
	missions = $state<MissionInfo[]>([]);
	missionId = $state('');
	seed = $state(0);
	#state = $state<GameState | null>(null);
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
	/** Installed scenario packs (imported CSVs and loaded .tlpack files), verified by the engine. */
	packs = $state<StoredPack[]>([]);

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
	/** Run headers for the journal screen (loaded when it opens). */
	journalRecords = $state<RunRecord<PlanEntry, never>[]>([]);

	private engine: EngineClient | null = null;
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
			this.engine = new EngineClient();
			const info = await this.engine.request({ kind: 'init' });
			this.missions = info.missions;
			this.policy = info.version.executionPolicy;
			await this.openStore();
			await this.refreshSaved();
			this.packs = (await this.store?.packs().catch(() => null)) ?? [];
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
	}

	private async refreshSaved(): Promise<void> {
		const finished = await this.store?.finishedMissions().catch(() => []);
		if (finished) this.completed = [...new Set([...this.completed, ...finished])];
		const run = await this.store?.activeRun().catch(() => null);
		this.saved =
			run && run.record.status === 'active'
				? { missionId: run.record.missionId, seed: run.record.seed, actions: run.actions.length, campaign: run.record.campaign ?? null }
				: null;
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
		if (!this.perksAllowed || !this.perksUnlocked.includes(id) || this.log.length > 0) return;
		await this.exclusive(async () => {
			this.perkChoice = on ? [...new Set([...this.perkChoice, id])] : this.perkChoice.filter((p) => p !== id);
			this.options = { ...this.options, ...perkOptions(this.perkChoice) };
			this.state = await this.eng().request({ kind: 'start', missionId: this.missionId, seed: this.seed, options: { ...this.options } });
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

	private async launch(
		missionId: string,
		seed: number,
		options: RunOptionsView,
		campaign: CampaignLink | null,
		perks: PerkId[] = []
	): Promise<void> {
		this.resetRun(missionId, seed);
		this.options = options;
		this.perkChoice = perks;
		this.runCampaign = campaign;
		this.state = await this.eng().request({ kind: 'start', missionId, seed, options: { ...options } });
		await this.createRunRecord(missionId, seed, { campaign: campaign ?? undefined });
		this.screen = this.needsTutorial(missionId) ? 'tutorial' : 'briefing';
	}

	// ---- curriculum map and journal screens (web-client task 4.4) ----

	openCurriculum(): void {
		this.screen = 'curriculum';
	}

	async openJournal(): Promise<void> {
		this.journalRecords = (await this.store?.runs().catch(() => null)) ?? [];
		this.screen = 'journal';
	}

	/** Practice run on an installed pack (e.g. an imported CSV); the pack is verified again on start. */
	async startPack(digest: string, seed = this.randomSeed()): Promise<void> {
		const pack = this.packs.find((p) => p.digest === digest);
		if (!pack) return;
		await this.exclusive(async () => {
			await this.settleAbandoned();
			this.resetRun(pack.scenarioId, seed);
			this.runPack = digest;
			this.options = { ...DEFAULT_RUN_OPTIONS }; // pack runs start without advisor or perks
			this.state = await this.eng().request({ kind: 'startPack', pack: pack.bytes, keys: await this.trusted(), seed });
			await this.createRunRecord(pack.scenarioId, seed, { packDigest: digest });
			this.screen = 'briefing';
		});
	}

	private async createRunRecord(
		missionId: string,
		seed: number,
		link: { packDigest?: string; campaign?: CampaignLink } = {}
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
				...(link.packDigest ? { packDigest: link.packDigest } : {}),
				...(link.campaign ? { campaign: { ...link.campaign } } : {})
			})
			.catch(() => (this.durable = false));
		void this.requestPersistence();
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

	async validateCsv(csv: string, meta: CsvMeta): Promise<CsvReport> {
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

	/** Verify-then-install: only bytes the engine accepted are stored (by digest). */
	async installPack(bytes: Uint8Array, label: string): Promise<PackCheck> {
		const check = await this.eng().request({ kind: 'packValidate', pack: bytes, keys: await this.trusted() });
		if (!check.ok || !check.pack) return check;
		const s = check.pack;
		const stored: StoredPack = {
			digest: s.digest,
			scenarioId: s.scenarioId,
			label,
			bytes,
			private: s.private,
			totalBars: s.totalBars,
			warmupBars: s.warmupBars,
			leverageCap: s.leverageCap,
			gaps: s.gaps,
			addedAt: Date.now()
		};
		await this.store?.savePack(stored).catch(() => (this.durable = false));
		this.packs = [...this.packs.filter((p) => p.digest !== stored.digest), stored];
		return check;
	}

	async removePack(digest: string): Promise<void> {
		await this.store?.deletePack(digest).catch(() => (this.durable = false));
		this.packs = this.packs.filter((p) => p.digest !== digest);
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
		this.options = { ...DEFAULT_RUN_OPTIONS, ...r.options };
		const state = await this.startEngine(r.missionId, r.seed, r.packDigest);
		if (!state) {
			this.menuAlert = t('pack.missing');
			this.saved = null;
			return;
		}
		this.resetRun(r.missionId, r.seed);
		this.runId = r.runId;
		this.runCampaign = r.campaign ?? null;
		this.runPack = r.packDigest ?? null;
		this.perkChoice = perksUsed(this.options);
		this.state = state;
		for (const a of run.actions) await this.exec(a, false);
		// Keep the replayed history so the next action gets the right sequence number.
		this.log = [...run.actions];
		this.journal = r.journal;
		this.stopWarnings = r.stopWarnings;
		if (r.executionPolicy !== this.policy) {
			this.notice = { tone: 'warn', text: `run recorded with execution policy v${r.executionPolicy}; engine is v${this.policy}` };
		} else if (this.state && r.digest && r.actions === run.actions.length && this.state.digest !== r.digest) {
			this.notice = { tone: 'warn', text: `replay digest mismatch (${r.digest.slice(0, 8)})` };
		}
		this.screen = this.state?.status === 'active' ? 'terminal' : 'debrief';
		if (this.screen === 'debrief') await this.loadDebrief();
		this.saved = null;
	}

	// A pack run restarts from its stored pack; null when the pack is no longer installed.
	private async startEngine(missionId: string, seed: number, packDigest?: string): Promise<GameState | null> {
		if (!packDigest) return this.eng().request({ kind: 'start', missionId, seed, options: { ...this.options } });
		const pack = (await this.store?.pack(packDigest).catch(() => null)) ?? this.packs.find((p) => p.digest === packDigest);
		if (!pack) return null;
		return this.eng().request({ kind: 'startPack', pack: pack.bytes, keys: await this.trusted(), seed });
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
		if (a.k === 'adv') {
			this.state = await e.request({ kind: 'advance', steps: a.n });
			return null;
		}
		const r = await e.request(commandRequest(a, this.clientId++));
		this.state = r.state;
		return r.result;
	}

	// In memory too: the menu must not wait for the fire-and-forget header write in persist().
	// A practice run on an imported pack is not a mission, so it never counts as completed.
	private markCompleted(): void {
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
		if (!s || !d || s.status === 'active' || s.knownScenario) return;
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
			if (r.status !== 'active' || r.packDigest) return null;
			const equity = r.equity ?? r.options?.startingBalance ?? '0';
			return { runId: r.runId, missionId: r.missionId, used: adviceUsed(stored.actions), campaign: r.campaign ?? null, equity };
		}
		const s = this.state;
		if (!s || s.status !== 'active' || s.knownScenario || !this.runId) return null;
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
