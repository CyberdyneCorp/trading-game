import { describe, expect, it } from 'vitest';
import { TUTORIAL_MISSIONS, tutorialFor } from './tutorials.ts';

import { campaignMissions } from './curriculum.ts';
import { liquidationPrice, type Costs } from './sizing.ts';

const MISSIONS = [
	'm01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer', 'm05-options-writer',
	'm06-thin-book', 'm07-trend-trailing', 'm08-regime-switch', 'm09-event-straddle', 'm10-hedged-book', 'm11-news-calendar',
	'm12-capstone', 'm13-call-spread', 'm14-covered-call', 'm15-protective-put', 'm16-range-condor', 'm17-before-expiry'
];
// What the scenarios were built to do is revealed only in the debrief: no outcome, no direction, no
// timing (m06 leaves its range, m07's trend direction, m08's switches, m09's drawn decision, m10's
// rumor, m11's slipping upgrade, m12's outage).
const SPOILERS =
	/rompimento falso|false breakout|incidente|incident|outage|falha de rede|choque|shock|crash|sai do range|leaves? the range|liquidez volta|liquidity returns|para cima|upward|tend[êe]ncia de alta|trend up|sem avisar|without warning|troca de regime|regime switch|rumor|surpresa forte|strong surprise|adiad|delayed|slips?\b|juros mantidos|rates on hold/i;

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

	it('the first mission introduces the advisor, its cost and how points are earned (spec mission-tutorial)', () => {
		// m06 opens the campaign (design D8.4); m01 keeps its card for free play.
		for (const id of [campaignMissions()[0], 'm01-false-breakout']) {
			const en = tutorialFor(id, 'en').find((c) => c.art === 'advisor');
			const pt = tutorialFor(id, 'pt-BR').find((c) => c.art === 'advisor');
			expect(en?.body, id).toMatch(/advisor.*1 point.*plan and a stop/s);
			expect(pt?.body, id).toMatch(/assessor.*1 ponto.*plano e stop/s);
		}
	});

	it('the campaign starts with m06, whose cards introduce the chart and the thin book (spec curriculum)', () => {
		expect(campaignMissions()[0]).toBe('m06-thin-book');
		const arts = tutorialFor('m06-thin-book', 'en').map((c) => c.art);
		expect(arts).toEqual(['chart', 'book', 'ticket', 'risk', 'advisor']);
		expect(tutorialFor('m07-trend-trailing', 'en').map((c) => c.art)).toContain('trail');
		expect(tutorialFor('m08-regime-switch', 'en').map((c) => c.art)).toContain('regime');
	});

	it('batch B cards: the straddle and the announced calendar (m09), the report first in the capstone (m12)', () => {
		const arts = (id: string) => tutorialFor(id, 'en').map((c) => c.art);
		expect(arts('m09-event-straddle')).toEqual(['calendar', 'straddle', 'volatility', 'decay', 'chain']);
		expect(arts('m10-hedged-book')).toEqual(['payoff', 'margin', 'funding', 'chain', 'risk']);
		expect(arts('m11-news-calendar')).toEqual(['calendar', 'news', 'risk', 'plan', 'advisor']);
		expect(arts('m12-capstone')).toEqual(['report', 'regime', 'calendar', 'funding', 'risk']);
		// m11's advisor card explains the EventAhead wait, not the m01 points intro.
		expect(tutorialFor('m11-news-calendar', 'en').find((c) => c.art === 'advisor')?.body).toMatch(/3 bars.*wait/s);
		expect(tutorialFor('m11-news-calendar', 'pt-BR').find((c) => c.art === 'advisor')?.body).toMatch(/3 barras.*esperar/s);
	});

	it('strategy cards (m13-m17): the new art first, the spec card list, never what the seed drew', () => {
		const arts = (id: string) => tutorialFor(id, 'en').map((c) => c.art);
		expect(arts('m13-call-spread')).toEqual(['spread', 'payoff', 'volatility', 'margin']);
		expect(arts('m14-covered-call')).toEqual(['covered', 'payoff', 'margin', 'funding']);
		expect(arts('m15-protective-put')).toEqual(['insurance', 'risk', 'chain', 'decay']);
		expect(arts('m16-range-condor')).toEqual(['condor', 'volatility', 'margin', 'risk', 'decay']);
		expect(arts('m17-before-expiry')).toEqual(['roll', 'decay', 'chain', 'plan']);
		// m15's insurance may or may not be needed: the cards never say a fall will come.
		for (const loc of ['pt-BR', 'en'] as const) {
			for (const c of tutorialFor('m15-protective-put', loc)) expect(c.body).not.toMatch(/will (fall|drop)|vai cair|haverá (uma )?queda/i);
		}
		expect(tutorialFor('m15-protective-put', 'en').find((c) => c.art === 'risk')?.body).toMatch(/may or may not/);
		expect(tutorialFor('m15-protective-put', 'pt-BR').find((c) => c.art === 'risk')?.body).toMatch(/pode vir ou não/);
	});

	it('every mission that writes options states its margin rule; the covered call is margined on its own (spec mission-tutorial)', () => {
		const card = (id: string, loc: 'pt-BR' | 'en') => tutorialFor(id, loc).map((c) => c.body).join(' ');
		for (const id of ['m05-options-writer', 'm10-hedged-book']) expect(card(id, 'en'), id).toMatch(/margin/);
		for (const id of ['m13-call-spread', 'm16-range-condor', 'm17-before-expiry']) {
			expect(card(id, 'en'), id).toMatch(/covered by a bought one of the same kind and expiry needs (margin )?only/);
			expect(card(id, 'pt-BR'), id).toMatch(/coberta por uma comprada do mesmo tipo e vencimento pede (margem )?só/);
		}
		expect(card('m14-covered-call', 'en')).toMatch(/written call needs its own margin even while the long covers it in P&L/);
		expect(card('m14-covered-call', 'pt-BR')).toMatch(/call vendida tem margem própria mesmo com o long cobrindo o resultado/);
	});

	it("m03's leverage card states the move that liquidates at 20x, maintenance margin included", () => {
		// Margin 5% of the notional, maintenance 0.5% (m03's contract): liquidation comes before a 5% move.
		const k: Costs = { multiplier: 1, takerFee: 0, maintenanceMargin: 0.005, halfSpread: 0, slippage: 0 };
		const long = 1 - (liquidationPrice('buy', 1, 100, 100 / 20, k) ?? 0) / 100;
		const short = (liquidationPrice('sell', 1, 100, 100 / 20, k) ?? 0) / 100 - 1;
		expect(long.toFixed(3)).toBe('0.045');
		expect(short.toFixed(3)).toBe('0.045');
		const card = (loc: 'pt-BR' | 'en') => tutorialFor('m03-volatility-shock', loc).find((c) => c.art === 'leverage')?.body;
		expect(card('pt-BR')).toMatch(/cerca de 4,5% contra você/);
		expect(card('en')).toMatch(/about 4\.5% against you/);
	});

	it('an unknown mission has no tutorial', () => {
		expect(tutorialFor('nope', 'en')).toEqual([]);
	});
});
