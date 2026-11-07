import { describe, expect, it } from 'vitest';
import { CoachModel } from './coach.svelte.ts';

const model = () => {
	const c = new CoachModel();
	let done = 0;
	c.onDone = () => (done += 1);
	return { c, done: () => done };
};

describe('guided first trade view-model (design D6)', () => {
	it('First campaign mission: starts at the chart, then the debrief step waits for that run', () => {
		const { c, done } = model();
		c.maybeStart('r1', true);
		expect(c.step?.target).toBe('chart');
		for (let i = 0; i < 5; i++) c.next();
		expect(c.waitingForDebrief).toBe(true);
		expect(done()).toBe(1); // the terminal part counts as done
		c.debriefOpened('r1');
		expect(c.step?.target).toBe('debrief');
		c.next();
		expect(c.active).toBe(false);
	});

	it('Once per device: no tour unless it is the first campaign terminal or it was requested', () => {
		const { c } = model();
		c.maybeStart('r1', false);
		expect(c.active).toBe(false);
		c.requested = true; // "Guided tour" on the briefing
		c.maybeStart('r2', false);
		expect(c.step?.target).toBe('chart');
		expect(c.requested).toBe(false);
	});

	it('skip marks it done; leaving the terminal mid-tour ends it, reaching the debrief jumps to its step', () => {
		const a = model();
		a.c.start('r1');
		a.c.skip();
		expect(a.c.active).toBe(false);
		expect(a.done()).toBe(1);
		const b = model();
		b.c.start('r1');
		b.c.leftTerminal(false);
		expect(b.c.active).toBe(false);
		expect(b.done()).toBe(0);
		const d = model();
		d.c.start('r1');
		d.c.leftTerminal(true);
		expect(d.c.waitingForDebrief).toBe(true);
		d.c.debriefOpened('other-run');
		expect(d.c.active).toBe(false);
	});
});
