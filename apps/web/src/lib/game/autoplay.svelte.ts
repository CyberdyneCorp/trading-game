// Auto-advance play mode (spec terminal-ui "Auto-advance"). View-model only: it calls the same
// advance the player would, one bar per tick, and never skips anything the engine decides.

export const AUTO_SPEEDS = [
	{ label: '1×', ms: 1500 },
	{ label: '2×', ms: 800 },
	{ label: '4×', ms: 400 },
	{ label: '10×', ms: 150 }
] as const;

/** Why auto-advance stopped by itself (shown to the player). */
export type PauseReason = 'fill' | 'liquidation' | 'margin_call' | 'option_settlement' | 'news' | 'finished';

export interface AutoPlayHost {
	/** The run is active and nothing (dialog, question, pick, pending command) blocks advancing. */
	ready(): boolean;
	finished(): boolean;
	advance(): Promise<void>;
	/** Something on the bar that just closed that the player should not miss, if any. */
	notable(): PauseReason | null;
}

export interface Timers {
	set(fn: () => void, ms: number): unknown;
	clear(handle: unknown): void;
}

const browserTimers: Timers = {
	set: (fn, ms) => setTimeout(fn, ms),
	clear: (h) => clearTimeout(h as ReturnType<typeof setTimeout>)
};

export class AutoPlay {
	running = $state(false);
	speed = $state(1); // index into AUTO_SPEEDS
	pauseOnEvents = $state(true);
	paused = $state<PauseReason | null>(null);

	private handle: unknown = null;

	constructor(
		private readonly host: AutoPlayHost,
		private readonly timers: Timers = browserTimers
	) {}

	start(): void {
		if (this.running || this.host.finished()) return;
		this.running = true;
		this.paused = null;
		this.schedule();
	}

	stop(reason: PauseReason | null = null): void {
		this.running = false;
		this.paused = reason;
		if (this.handle != null) this.timers.clear(this.handle);
		this.handle = null;
	}

	toggle(): void {
		if (this.running) this.stop();
		else this.start();
	}

	setSpeed(index: number): void {
		this.speed = Math.min(Math.max(0, index), AUTO_SPEEDS.length - 1);
	}

	private schedule(): void {
		this.handle = this.timers.set(() => void this.tick(), AUTO_SPEEDS[this.speed].ms);
	}

	/** One step: wait while blocked, advance one bar, then pause on anything notable. */
	async tick(): Promise<void> {
		this.handle = null;
		if (!this.running) return;
		if (this.host.finished()) return this.stop('finished');
		if (this.host.ready()) {
			await this.host.advance();
			if (!this.running) return; // stopped while the bar was being computed
			if (this.host.finished()) return this.stop('finished');
			const reason = this.pauseOnEvents ? this.host.notable() : null;
			if (reason) return this.stop(reason);
		}
		this.schedule();
	}
}
