import { describe, expect, it } from 'vitest';
import type { DebriefData, EngineFinding, PositionEpisode } from '#lib/engine/types.ts';
import { describeFinding } from './analysis.ts';
import { excursionsText } from './episodeText.ts';
import { objectiveText } from './objectives.ts';
import { findingMoment, objectiveReadings } from './objectiveChecks.ts';

const episode = (over: Partial<PositionEpisode> = {}): PositionEpisode => ({
	n: 1,
	side: 'long',
	entryStep: 75,
	exitStep: 90,
	entries: [{ step: 76, qty: '25', price: '100' }],
	exits: [{ step: 90, qty: '25', price: '104' }],
	stopMoves: [],
	trailing: false,
	net: '100',
	plannedRisk: '100',
	rBp: 10000,
	mfeRBp: 12000,
	maeRBp: 3000,
	...over
});

const finding = (code: EngineFinding['code'], step: number, evidence?: EngineFinding['evidence']): EngineFinding => ({
	code,
	tone: 'warn',
	step,
	count: 0,
	price: '0',
	requested: '0',
	amount: '0',
	...(evidence ? { evidence } : {})
});

function debrief(objectives: [string, number][], over: Partial<DebriefData> = {}): DebriefData {
	return {
		missionRules: { objectives: objectives.map(([code, valueBp]) => ({ code, valueBp })) },
		findings: [],
		episodes: [],
		...over
	} as unknown as DebriefData;
}

const M22: [string, number][] = [['total_risk_all_entries', 100], ['partial_exit_or_trail', 0], ['never_stop_away', 0]];

describe('objectives read from the engine facts (m22-m25, scoring v14)', () => {
	it('m22: total risk within 1%, a partial exit and no stop moved away are met', () => {
		const d = debrief(M22, { episodes: [episode({ exits: [{ step: 80, qty: '12.5', price: '104' }, { step: 90, qty: '12.5', price: '100' }] })] });
		expect(objectiveReadings(d, '10000').map((r) => r.verdict)).toEqual(['met', 'met', 'met']);
	});

	it('m22: an unplanned or oversized idea, a single exit and a stop moved away are missed (with the bar)', () => {
		const d = debrief(M22, {
			episodes: [episode({ plannedRisk: '150', stopMoves: [{ step: 82, kind: 'away', price: '95' }] })]
		});
		const r = objectiveReadings(d, '10000');
		expect(r.map((x) => x.verdict)).toEqual(['missed', 'missed', 'missed']);
		expect(r[2].step).toBe(82);
		expect(objectiveReadings(debrief(M22, { episodes: [episode({ plannedRisk: '0' })] }), '10000')[0].verdict).toBe('missed');
	});

	it('m22: a trailing stop counts as the second rule; no trade leaves the risk and exit objectives unjudged', () => {
		expect(objectiveReadings(debrief(M22, { episodes: [episode({ trailing: true })] }), '10000')[1].verdict).toBe('met');
		expect(objectiveReadings(debrief(M22), '10000').map((r) => r.code)).toEqual(['never_stop_away']);
	});

	it('m23-m25: the bias finding misses its objective at its bar; without it the objective is met', () => {
		const revenge = finding('revenge_trade', 83, { trade: 1, exitStep: 83, loss: '112.81', entryStep: 83, sizeRatioBp: 20000, planned: false });
		expect(objectiveReadings(debrief([['cooldown_after_stop', 0]], { findings: [revenge] }), '10000')).toEqual([
			{ code: 'cooldown_after_stop', verdict: 'missed', step: 83 }
		]);
		expect(objectiveReadings(debrief([['cooldown_after_stop', 0]]), '10000')[0].verdict).toBe('met');
		expect(objectiveReadings(debrief([['plan_before_entry_atr_stop', 0]], { findings: [finding('no_plan', 0)] }), '1')[0].verdict).toBe('missed');
		expect(objectiveReadings(debrief([['no_add_beyond_plan', 0]], { findings: [finding('averaging_down', 128)] }), '1')[0].step).toBe(128);
		expect(objectiveReadings(debrief([['risk_per_idea', 100]]), '1')).toEqual([]); // not judged here
	});

	it('m23: the cooldown objective and the revenge finding name a losing close, as the rule reads it (W2-X2: a manual losing close read as a missed "stop" objective)', () => {
		const pt = objectiveText({ code: 'cooldown_after_stop', valueBp: 0 }, 'pt-BR');
		const en = objectiveText({ code: 'cooldown_after_stop', valueBp: 0 }, 'en');
		expect(pt).toMatch(/trade fechado com perda/);
		expect(en).toMatch(/losing close/);
		expect(`${pt} ${en}`).not.toMatch(/\bstop\b/);
		const f = finding('revenge_trade', 62, { trade: 1, exitStep: 62, loss: '8.31', entryStep: 62, sizeRatioBp: 20000, planned: false });
		expect(`${describeFinding(f, 'pt-BR')} ${describeFinding(f, 'en')}`).not.toMatch(/\bstop/);
	});

	it('m22: two stops filling on the closing bar are one exit, not a partial one (W2-X2: read as met)', () => {
		const together = episode({ exits: [{ step: 90, qty: '10', price: '99.02' }, { step: 90, qty: '15', price: '99.01' }] });
		expect(objectiveReadings(debrief(M22, { episodes: [together] }), '10000')[1]).toEqual({ code: 'partial_exit_or_trail', verdict: 'missed' });
		const scaledOut = episode({ exits: [{ step: 84, qty: '10', price: '103' }, { step: 90, qty: '15', price: '104' }] });
		expect(objectiveReadings(debrief(M22, { episodes: [scaledOut] }), '10000')[1].verdict).toBe('met');
		const open = episode({ exitStep: null, exits: [{ step: 84, qty: '10', price: '103' }] });
		expect(objectiveReadings(debrief(M22, { episodes: [open] }), '10000')[1].verdict).toBe('met');
	});

	it('the episodes panel reads the adverse excursion as a loss in R (W2-X2: shown as +2,03R)', () => {
		expect(excursionsText({ mfeRBp: 3300, maeRBp: 20300 }, 'pt-BR')).toBe('+0,33R · -2,03R');
		expect(excursionsText({ mfeRBp: 0, maeRBp: 0 }, 'en')).toBe('0.00R · 0.00R');
		expect(excursionsText({ mfeRBp: null, maeRBp: null }, 'en')).toBe('— · —');
	});

	it('a debrief from an engine before scoring v14 (no episodes) is read without failing', () => {
		const old = { ...debrief(M22), episodes: undefined } as unknown as DebriefData;
		expect(objectiveReadings(old, '10000').map((r) => r.verdict)).toEqual(['met']);
	});
});

describe('bias findings as text, with their evidence (scoring v14)', () => {
	it('names the revenge trade with the losing trade, its loss, the bars and the size ratio', () => {
		const f = finding('revenge_trade', 83, { trade: 1, exitStep: 83, loss: '112.81024645', entryStep: 83, sizeRatioBp: 20000, planned: false });
		expect(describeFinding(f, 'en')).toBe(
			'Revenge trade: trade 1 closed at a loss of 112.81 cr on bar 83, and on bar 83 a new entry came with no plan recorded (2.00× its size). After a loss, wait for the setup to form again and enter at normal size.'
		);
		expect(describeFinding({ ...f, evidence: { ...f.evidence!, planned: true, sizeRatioBp: 15000 } }, 'pt-BR')).toMatch(/com 1,50× o tamanho dele/);
		expect(findingMoment(f)).toBe(83);
	});

	it('names the chase with its closes, extension in ATR and stop; averaging down with the averages and risks', () => {
		const fomo = finding('fomo_entry', 98, { entryStep: 98, extensionAtr: 300, closes: 4, stop: '99.59' });
		expect(describeFinding(fomo, 'en')).toMatch(/bar 98 came after 4 closes in a row the same way, 3.00 ATR from the 20-bar average, with the stop at 99.59, more than 2 ATR away/);
		expect(describeFinding({ ...fomo, evidence: { ...fomo.evidence!, stop: '0' } }, 'pt-BR')).toMatch(/a 3,00 ATR da média de 20 barras, sem stop/);
		const avg = finding('averaging_down', 128, {
			addStep: 128, avgBefore: '100.74', avgAfter: '99.84', unrealized: '44.1', riskBefore: '107.1', riskAfter: '236.25', planRisk: '99.77', stop: '97.34', planned: false
		});
		expect(describeFinding(avg, 'en')).toMatch(/on bar 128 you added to a position losing 44.10 cr, and the average entry went from 100.74 to 99.84; the risk to the stop went from 107.10 to 236.25 cr \(the first entry’s plan: 99.77 cr\)/);
		expect(findingMoment(avg)).toBe(128);
	});

	it('names the exceeded risk limit, its value, the closes and the worst level', () => {
		const f = { ...finding('risk_limit_exceeded', 91, { limit: 'delta', limitBp: 1500, worstBp: 2375 }), count: 83 };
		expect(describeFinding(f, 'en')).toBe('The delta risk limit (15% of equity) was exceeded at 83 closes, from bar 91 (worst: 23.75%). Reduce or hedge before the limit is crossed.');
		expect(describeFinding(f, 'pt-BR')).toMatch(/O limite de risco de delta \(15% do patrimônio\) foi passado em 83 fechamentos/);
	});
});
