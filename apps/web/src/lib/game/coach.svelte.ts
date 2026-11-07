// View-model of the guided first trade (design D6, guided-first-trade spec). The tour runs on the
// real terminal of one run: steps 1–5 there, the last when that run's debrief opens. It starts by
// itself on the first campaign terminal of the device, or when the player asks for it ("Guided tour"
// in the help overlay or the briefing). Skipping, or reaching the debrief step, marks it done on the
// device; leaving the terminal before the advance step ends it without marking it.

import { FIRST_TRADE, TERMINAL_STEPS, type CoachStep } from '#lib/content/coach.ts';

export class CoachModel {
	/** Index of the step shown; null = no tour. */
	index = $state<number | null>(null);
	/** The run the tour belongs to. */
	runId = '';
	/** "Guided tour" was chosen on the briefing: start when the terminal opens. */
	requested = false;
	/** Called once when the tour counts as done (skipped, or the terminal part finished). */
	onDone: (() => void) | null = null;

	get active(): boolean {
		return this.index !== null;
	}

	get step(): CoachStep | null {
		return this.index === null ? null : (FIRST_TRADE.steps[this.index] ?? null);
	}

	/** The terminal part is over; the debrief step waits for this run's debrief. */
	get waitingForDebrief(): boolean {
		return this.index === TERMINAL_STEPS;
	}

	start(runId: string): void {
		this.runId = runId;
		this.requested = false;
		this.index = 0;
	}

	/** On a terminal that just opened: start when asked to, or on a first campaign terminal. */
	maybeStart(runId: string, firstCampaignTerminal: boolean): void {
		if (this.active && this.runId === runId) return;
		if (this.requested || firstCampaignTerminal) this.start(runId);
		else this.index = null;
	}

	next(): void {
		if (this.index === null) return;
		const n = this.index + 1;
		if (n === TERMINAL_STEPS) this.onDone?.();
		this.index = n < FIRST_TRADE.steps.length ? n : null;
	}

	skip(): void {
		if (this.index === null) return;
		this.index = null;
		this.onDone?.();
	}

	/** The terminal closed: to this run's debrief, the tour jumps to its debrief step; anywhere else it ends. */
	leftTerminal(toDebrief: boolean): void {
		if (this.index === null || this.index >= TERMINAL_STEPS) return;
		if (toDebrief) {
			this.index = TERMINAL_STEPS;
			this.onDone?.();
		} else this.index = null;
	}

	/** A debrief opened: the waiting step shows only for the tour's own run. */
	debriefOpened(runId: string): void {
		if (this.waitingForDebrief && runId !== this.runId) this.index = null;
	}
}

export const coach = new CoachModel();
