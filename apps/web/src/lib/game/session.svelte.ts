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

export type Screen = 'loading' | 'menu' | 'briefing' | 'terminal' | 'debrief';

type Action =
	| { k: 'lev'; lev: number }
	| { k: 'order'; type: OrderTypeName; side: OrderSideName; qty: string; price: string | null; ro: boolean }
	| { k: 'cancel'; id: number }
	| { k: 'close' }
	| { k: 'adv'; n: number };

interface SavedRun {
	v: 1;
	missionId: string;
	seed: number;
	log: Action[];
	journal: PlanEntry[];
	stopWarnings: number;
	digest: string;
}

export interface Protection {
	side: OrderSideName; // entry side
	stop: string | null;
	takeProfit: string | null;
}

export interface Notice {
	tone: 'ok' | 'warn' | 'error';
	text: string;
}

const STORAGE_KEY = 'tradelab.run.v1';

function loadSaved(): SavedRun | null {
	try {
		const raw = localStorage.getItem(STORAGE_KEY);
		return raw ? (JSON.parse(raw) as SavedRun) : null;
	} catch {
		return null;
	}
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
	protection = $state<Protection | null>(null);
	notice = $state<Notice | null>(null);
	stopWarnings = $state(0);
	busy = $state(false);
	saved = $state<SavedRun | null>(null);

	private engine: EngineClient | null = null;
	private log: Action[] = [];
	private clientId = 1;

	async init(): Promise<void> {
		try {
			this.engine = new EngineClient();
			const info = await this.engine.request({ kind: 'init' });
			this.missions = info.missions;
			this.saved = loadSaved();
			this.screen = 'menu';
		} catch (e) {
			this.error = e instanceof Error ? e.message : String(e);
		}
	}

	dispose(): void {
		this.engine?.dispose();
	}

	/** Optional fixed seed (from `?seed=` in the URL) for reproducible sessions and tests. */
	fixedSeed: number | null = null;

	async startMission(missionId: string, seed = this.fixedSeed ?? Math.floor(Math.random() * 1_000_000_000)): Promise<void> {
		this.resetRun(missionId, seed);
		this.state = await this.eng().request({ kind: 'start', missionId, seed });
		this.persist();
		this.screen = 'briefing';
	}

	async resume(): Promise<void> {
		const s = this.saved;
		if (!s) return;
		this.resetRun(s.missionId, s.seed);
		this.state = await this.eng().request({ kind: 'start', missionId: s.missionId, seed: s.seed });
		for (const a of s.log) await this.exec(a, false);
		// Keep the replayed history: the next save must contain the full log, not only new actions.
		this.log = [...s.log];
		this.journal = s.journal;
		this.stopWarnings = s.stopWarnings;
		if (this.state && s.digest && this.state.digest !== s.digest) {
			this.notice = { tone: 'warn', text: `replay digest mismatch (${s.digest.slice(0, 8)})` };
		}
		this.screen = this.state?.status === 'active' ? 'terminal' : 'debrief';
		if (this.screen === 'debrief') await this.loadDebrief();
	}

	enterTerminal(): void {
		this.screen = 'terminal';
	}

	backToMenu(): void {
		this.saved = loadSaved();
		this.screen = 'menu';
	}

	async setLeverage(lev: number): Promise<CommandResult | null> {
		return this.command({ k: 'lev', lev });
	}

	async placeOrder(
		type: OrderTypeName,
		side: OrderSideName,
		qty: string,
		price: string | null,
		reduceOnly = false
	): Promise<CommandResult | null> {
		const res = await this.command({ k: 'order', type, side, qty, price, ro: reduceOnly });
		if (res?.warning === 'stop_beyond_liquidation') this.stopWarnings += 1;
		return res;
	}

	async cancel(orderId: number): Promise<CommandResult | null> {
		return this.command({ k: 'cancel', id: orderId });
	}

	async closePosition(): Promise<CommandResult | null> {
		this.protection = null;
		return this.command({ k: 'close' });
	}

	recordPlan(plan: PlanEntry): void {
		this.journal = [...this.journal, plan];
		this.persist();
	}

	async advance(n: number): Promise<void> {
		if (!this.state || this.state.status !== 'active' || this.busy) return;
		this.busy = true;
		try {
			await this.exec({ k: 'adv', n }, true);
			await this.attachProtection();
			this.persist();
			if (this.state && this.state.status !== 'active') {
				await this.loadDebrief();
				this.screen = 'debrief';
			}
		} finally {
			this.busy = false;
		}
	}

	private resetRun(missionId: string, seed: number): void {
		this.missionId = missionId;
		this.seed = seed;
		this.log = [];
		this.clientId = 1;
		this.journal = [];
		this.protection = null;
		this.notice = null;
		this.debrief = null;
		this.stopWarnings = 0;
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
		}
	}

	// Runs one action against the engine. `live` = user action (logged); replay passes false.
	private async exec(a: Action, live: boolean): Promise<CommandResult | null> {
		if (live) this.log.push(a);
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
					? await e.request({ kind: 'order', clientId, type: a.type, side: a.side, qty: a.qty, price: a.price, reduceOnly: a.ro })
					: a.k === 'cancel'
						? await e.request({ kind: 'cancel', clientId, orderId: a.id })
						: await e.request({ kind: 'close', clientId });
		this.state = r.state;
		return r.result;
	}

	// UI-level bracket: once the entry has filled, send the planned stop and target as reduce-only.
	private async attachProtection(): Promise<void> {
		const p = this.protection;
		const s = this.state;
		if (!p || !s) return;
		const qty = Number(s.position.qty);
		if (qty === 0) return;
		const filledLong = qty > 0;
		if (filledLong !== (p.side === 'buy')) return;
		this.protection = null;
		const exitSide: OrderSideName = filledLong ? 'sell' : 'buy';
		const size = s.position.qty.replace('-', '');
		if (p.stop) await this.exec({ k: 'order', type: 'stop_market', side: exitSide, qty: size, price: p.stop, ro: true }, true);
		if (p.takeProfit) {
			await this.exec({ k: 'order', type: 'take_profit', side: exitSide, qty: size, price: p.takeProfit, ro: true }, true);
		}
	}

	private async loadDebrief(): Promise<void> {
		this.debrief = await this.eng().request({ kind: 'debrief' });
	}

	private persist(): void {
		if (!this.state) return;
		const data: SavedRun = {
			v: 1,
			missionId: this.missionId,
			seed: this.seed,
			log: this.log,
			journal: this.journal,
			stopWarnings: this.stopWarnings,
			digest: this.state.digest
		};
		try {
			localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
		} catch {
			// Storage can be unavailable (private mode); the run still works, just not resumable.
		}
	}
}
