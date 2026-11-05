import { describe, expect, it } from 'vitest';
import type { EngineTrade, RevealEvent } from '#lib/engine/types.ts';
import { fmtBp, releaseRows } from './debrief.ts';
import { headline, kindName, level } from './news.ts';

const event = (step: number, extra: Partial<RevealEvent> = {}): RevealEvent => ({
	step,
	kind: 'rate_decision',
	actual: 1,
	surprise: 1,
	intensityPct: 120,
	reaction: -1,
	expectation: 0,
	credibility: 80,
	announceLead: 20,
	gapBp: 235,
	moveBp: -120,
	impliedBp: -1,
	...extra
});

const trade = (entryStep: number, exitStep: number | null) => ({ entryStep, exitStep }) as EngineTrade;

describe('per-release reveal', () => {
	it('weights the surprise by credibility and says whether the player was positioned before or entered after', () => {
		const rows = releaseRows([event(80), event(120), event(150), event(170)], [trade(70, 85), trade(125, 140), trade(165, null)], 200);
		expect(rows.map((r) => r.weighted)).toEqual([0.8, 0.8, 0.8, 0.8]);
		expect(rows.map((r) => r.you)).toEqual(['before', 'after', 'flat', 'before']);
		expect(releaseRows([event(80)], [trade(70, 82), trade(84, 90)], 200)[0].you).toBe('both');
	});

	it('a trade exited on the release bar was not held through it', () => {
		expect(releaseRows([event(80)], [trade(70, 80)], 200)[0].you).toBe('flat');
	});

	it('lists only releases the run reached (a game over ends the reveal early)', () => {
		expect(releaseRows([event(80), event(150)], [], 100).map((r) => r.step)).toEqual([80]);
	});

	it('formats basis points with the PT-BR decimal comma', () => {
		expect(fmtBp(235, 'pt-BR')).toBe('+2,35%');
		expect(fmtBp(-120, 'en')).toBe('-1.20%');
	});
});

describe('batch B news', () => {
	it('names the new release kinds and reads their tone against the consensus', () => {
		expect(kindName('macro_data', 'pt-BR')).toBe('Índice de atividade');
		expect(kindName('network_upgrade', 'en')).toBe('Network upgrade');
		const upgrade = { step: 1, kind: 'network_upgrade', tone: -1, credibility: 85, expectation: 1, actual: 0, early: false } as const;
		expect(headline(upgrade, 'pt-BR')).toMatch(/adiada/);
		expect(headline({ ...upgrade, tone: 0, actual: 1 }, 'en')).toMatch(/as planned/);
		expect(headline({ ...upgrade, kind: 'macro_data', tone: 0 }, 'en')).toMatch(/in line/);
		expect(level(1, 'pt-BR')).toBe('positivo');
	});
});
