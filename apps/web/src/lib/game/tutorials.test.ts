import { describe, expect, it } from 'vitest';
import { TUTORIAL_MISSIONS, tutorialFor } from './tutorials.ts';

const MISSIONS = ['m01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer', 'm05-options-writer'];
// What the scenarios were built to do is revealed only in the debrief.
const SPOILERS = /rompimento falso|false breakout|incidente|incident|outage|falha de rede|choque|shock|crash/i;

// A sentence ends at . ! ? followed by a capital letter or the end ("LIQ? mostra" is one sentence).
const sentences = (s: string) => s.split(/[.!?](?:\s+(?=[A-ZÀ-Ú"])|$)/).filter((x) => x.trim().length > 0).length;

describe('mission tutorials', () => {
	it('every built-in mission has 3 to 5 cards in both locales, drawn the same way', () => {
		expect([...TUTORIAL_MISSIONS].sort()).toEqual(MISSIONS);
		for (const id of MISSIONS) {
			const pt = tutorialFor(id, 'pt-BR');
			const en = tutorialFor(id, 'en');
			expect(pt.length).toBeGreaterThanOrEqual(3);
			expect(pt.length).toBeLessThanOrEqual(5);
			expect(en.map((c) => c.art)).toEqual(pt.map((c) => c.art));
		}
	});

	it('cards are short (at most three sentences) and never spoil the scenario', () => {
		for (const id of MISSIONS) {
			for (const loc of ['pt-BR', 'en'] as const) {
				for (const c of tutorialFor(id, loc)) {
					expect(c.title.length).toBeGreaterThan(0);
					expect(sentences(c.body), c.body).toBeLessThanOrEqual(3);
					expect(`${c.title} ${c.body}`).not.toMatch(SPOILERS);
				}
			}
		}
	});

	it('an unknown mission has no tutorial', () => {
		expect(tutorialFor('nope', 'en')).toEqual([]);
	});
});
