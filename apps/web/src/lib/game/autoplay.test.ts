import { describe, expect, it } from 'vitest';
import { AutoPlay, type AutoPlayHost, type PauseReason, type Timers } from './autoplay.svelte.ts';

class FakeTimers implements Timers {
	pending: (() => void)[] = [];
	set(fn: () => void): unknown {
		this.pending.push(fn);
		return fn;
	}
	clear(h: unknown): void {
		this.pending = this.pending.filter((f) => f !== h);
	}
	async fire(): Promise<void> {
		const fn = this.pending.shift();
		fn?.();
		await new Promise((r) => setTimeout(r, 0));
	}
}

function host(over: Partial<AutoPlayHost> & { events?: (PauseReason | null)[] } = {}) {
	const state = { step: 0, ready: true, end: 10 };
	const events = over.events ?? [];
	const h: AutoPlayHost = {
		ready: () => state.ready,
		finished: () => state.step >= state.end,
		advance: async () => {
			state.step++;
		},
		notable: () => events[state.step - 1] ?? null,
		...over
	};
	return { h, state };
}

describe('auto-advance', () => {
	it('advances one bar per tick until stopped', async () => {
		const t = new FakeTimers();
		const { h, state } = host();
		const auto = new AutoPlay(h, t);
		auto.start();
		await t.fire();
		await t.fire();
		expect(state.step).toBe(2);
		auto.stop();
		expect(t.pending).toHaveLength(0);
		expect(auto.running).toBe(false);
	});

	it('waits without advancing while a dialog blocks, then resumes', async () => {
		const t = new FakeTimers();
		const { h, state } = host();
		const auto = new AutoPlay(h, t);
		state.ready = false;
		auto.start();
		await t.fire();
		await t.fire();
		expect(state.step).toBe(0);
		expect(auto.running).toBe(true);
		state.ready = true;
		await t.fire();
		expect(state.step).toBe(1);
	});

	it('pauses on notable events unless told not to', async () => {
		const t = new FakeTimers();
		const { h, state } = host({ events: [null, 'fill', null, 'news'] });
		const auto = new AutoPlay(h, t);
		auto.start();
		await t.fire();
		await t.fire();
		expect(state.step).toBe(2);
		expect(auto.running).toBe(false);
		expect(auto.paused).toBe('fill');
		auto.pauseOnEvents = false;
		auto.start();
		for (let i = 0; i < 3; i++) await t.fire();
		expect(state.step).toBe(5); // ran through the news bar
	});

	it('stops at the end of the mission and cannot start after it', async () => {
		const t = new FakeTimers();
		const { h, state } = host();
		state.end = 1;
		const auto = new AutoPlay(h, t);
		auto.start();
		await t.fire();
		expect(auto.paused).toBe('finished');
		auto.start();
		expect(auto.running).toBe(false);
	});

	it('a stop during an in-flight advance wins (no extra tick scheduled)', async () => {
		const t = new FakeTimers();
		let auto!: AutoPlay;
		const { h } = host({
			advance: async () => {
				auto.stop();
			}
		});
		auto = new AutoPlay(h, t);
		auto.start();
		await t.fire();
		expect(t.pending).toHaveLength(0);
	});
});
