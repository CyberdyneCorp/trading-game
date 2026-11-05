import protocol from './protocol.ts?raw';
import worker from './engine.worker.ts?raw';
import { describe, expect, it } from 'vitest';
import { game } from '#lib/game/fixtures.test-util.ts';
import { REVEAL_FIELDS, revealAllowed } from './reveal.ts';

describe('seeded reveal never crosses before the run ends (design D6)', () => {
	it('is allowed only once the run is finished or bankrupt', () => {
		expect(revealAllowed('active')).toBe(false);
		expect(revealAllowed('finished')).toBe(true);
		expect(revealAllowed('bankrupt')).toBe(true);
	});

	it('the worker calls tl_mission_reveal in one place, behind the status gate', () => {
		const calls = worker.match(/a\.reveal\(/g) ?? [];
		expect(calls).toHaveLength(1);
		const fn = worker.slice(worker.indexOf('function reveal(a: Api)'), worker.indexOf('function withState'));
		expect(fn).toContain("if (!revealAllowed(status)) throw new Error('run_active');");
		expect(fn.indexOf('revealAllowed')).toBeLessThan(fn.indexOf('a.reveal('));
	});

	it('only the reveal and debrief replies may carry reveal fields; the run state carries none', () => {
		// Message schema: the request kinds whose reply is the reveal or the debrief (both refused while active).
		const kinds = [...protocol.matchAll(/kind: '(\w+)'/g)].map((m) => m[1]);
		expect(kinds).toContain('reveal');
		const state = game([], { status: 'active' });
		for (const field of REVEAL_FIELDS) expect(Object.keys(state)).not.toContain(field);
	});
});
