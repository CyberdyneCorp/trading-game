// Game session view-model (MVVM): owns the engine client, the action log and screen flow.
// Resume = deterministic replay of the logged actions (same mission, seed and commands =>
// same state digest), so only the log is persisted, never balances.

import { EngineClient } from '#lib/engine/client.ts';
import type {
	CommandResult,
	DebriefData,
	GameState,
	MissionInfo,
	OrderSideName,
	OrderTypeName
} from '#lib/engine/types.ts';
import type { PlanEntry } from './analysis';
import { stepsUntilNextQuestion } from './calibration.ts';
import { newRunId, openDb, RunStore } from './storage.ts';

export type Screen = 'loading' | 'menu' | 'briefing' | 'terminal' | 'debrief';

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
	| { k: 'close' }
	| { k: 'adv'; n: number };

/** Summary of the resumable run shown on the menu. */
export interface RunSummary {
	missionId: string;
	seed: number;
	actions: number;
}

/** Optional order extras: bracket exits (engine-side OCO) and trailing distance. */
export interface OrderExtras {
	stop?: string | null;
	takeProfit?: string | null;
	trail?: string | null;
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
	state = $state<GameState | null>(null);
	debrief = $state<DebriefData | null>(null);
	journal = $state<PlanEntry[]>([]);
	notice = $state<Notice | null>(null);
	stopWarnings = $state(0);
	/** A calibration question due now (engine schedule) blocks advancing until answered. */
	pendingQuestion = $derived(this.state?.calibration.due ?? false);
	busy = $state(false);
	saved = $state<RunSummary | null>(null);
	/** False when IndexedDB is unavailable (e.g. some private modes): runs cannot be resumed. */
	durable = $state(true);
	persisted = $state<boolean | null>(null);

	private engine: EngineClient | null = null;
	private log: Action[] = [];
	private clientId = 1;
	private store: RunStore<Action, PlanEntry, never> | null = null;
	private runId = '';
	private policy = 0;

	async init(): Promise<void> {
		try {
			this.engine = new EngineClient();
			const info = await this.engine.request({ kind: 'init' });
			this.missions = info.missions;
			this.policy = info.version.executionPolicy;
			await this.openStore();
			await this.refreshSaved();
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
		const run = await this.store?.activeRun().catch(() => null);
		this.saved =
			run && run.record.status === 'active'
				? { missionId: run.record.missionId, seed: run.record.seed, actions: run.actions.length }
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

	async startMission(missionId: string, seed = this.fixedSeed ?? Math.floor(Math.random() * 1_000_000_000)): Promise<void> {
		await this.exclusive(async () => {
			this.resetRun(missionId, seed);
			this.state = await this.eng().request({ kind: 'start', missionId, seed });
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
					digest: this.state.digest,
					status: 'active',
					actions: 0
				})
				.catch(() => (this.durable = false));
			void this.requestPersistence();
			this.screen = 'briefing';
		});
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
		this.resetRun(r.missionId, r.seed);
		this.runId = r.runId;
		this.state = await this.eng().request({ kind: 'start', missionId: r.missionId, seed: r.seed });
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

	enterTerminal(): void {
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
			trail: extras.trail ?? null
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

	async closePosition(): Promise<CommandResult | null> {
		return this.command({ k: 'close' });
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
		this.missionId = missionId;
		this.seed = seed;
		this.log = [];
		this.clientId = 1;
		this.journal = [];
		this.notice = null;
		this.debrief = null;
		this.stopWarnings = 0;
		this.queued = 0;
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
		const clientId = this.clientId++;
		const r =
			a.k === 'lev'
				? await e.request({ kind: 'leverage', clientId, leverage: a.lev })
				: a.k === 'order'
					? await e.request({
							kind: 'order',
							clientId,
							type: a.type,
							side: a.side,
							qty: a.qty,
							price: a.price,
							reduceOnly: a.ro,
							attachStop: a.stop ?? null,
							attachTp: a.tp ?? null,
							trailDistance: a.trail ?? null
						})
					: a.k === 'opt'
						? await e.request({
								kind: 'option',
								clientId,
								series: a.series,
								side: a.side,
								type: a.type,
								qty: a.qty,
								premium: a.premium
							})
						: a.k === 'plan'
						? await e.request({
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
							})
						: a.k === 'ans'
						? await e.request({ kind: 'answer', clientId, pBp: a.pBp, skipped: a.skipped })
						: a.k === 'margin'
						? await e.request({ kind: 'margin', clientId, amount: a.amount })
						: a.k === 'cancel'
						? await e.request({ kind: 'cancel', clientId, orderId: a.id })
						: await e.request({ kind: 'close', clientId });
		this.state = r.state;
		return r.result;
	}

	private async loadDebrief(): Promise<void> {
		this.debrief = await this.eng().request({ kind: 'debrief' });
	}

	// Header fields only (journal, questions, digest, status); actions are committed in exec().
	private persist(): void {
		if (!this.state || !this.store || !this.runId) return;
		void this.store
			.updateRun(this.runId, {
				journal: $state.snapshot(this.journal),
				stopWarnings: this.stopWarnings,
				digest: this.state.digest,
				status: this.state.status
			})
			.catch(() => (this.durable = false));
	}
}
