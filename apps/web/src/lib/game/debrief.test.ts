import { describe, expect, it } from 'vitest';
import type { EngineFinding, EngineTrade, NewsView, PackReveal, RegimeSpan } from '#lib/engine/types.ts';
import { describeFinding, describeFindings, describeLesson } from './analysis.ts';
import { decisionCounts, entryRegimeName, fmtLeverageBp, fmtLiqDistance, greekRows, newsTimeline, openPnl, revealTexts, stopAdvice } from './debrief.ts';
import { headline } from './news.ts';
import { i18n } from './i18n.svelte.ts';

const perp: EngineTrade = {
	n: 1,
	series: 0,
	side: 'long',
	entryStep: 61,
	exitStep: 64,
	qty: '10.00000000',
	entryAvg: '100.00000000',
	exitAvg: '95.96000000',
	pnl: '-50.00000000',
	fees: '0',
	funding: '0',
	net: '-50.00000000',
	bars: 3,
	reason: 'liquidation',
	plannedRisk: '50.00000000',
	rBp: -10000,
	planRespected: false,
	stop: '95.00000000',
	leverage: 20,
	liqPrice: '95.96000000',
	stopBeyondLiq: true,
	safeLeverage: 16
};

describe('greekRows', () => {
	it('lists delta, gamma, theta, vega and residual in order, as numbers', () => {
		const rows = greekRows({ delta: '12.5', gamma: '0.25', theta: '-3.10', vega: '-0.9', residual: '-0.34', total: '8.41' });
		expect(rows.map((r) => r.key)).toEqual(['delta', 'gamma', 'theta', 'vega', 'residual']);
		expect(rows.map((r) => r.value)).toEqual([12.5, 0.25, -3.1, -0.9, -0.34]);
	});
	it('is empty when no option was traded', () => {
		expect(greekRows(null)).toEqual([]);
	});
});

describe('debrief numbers follow the locale', () => {
	it('formats leverage from basis points', () => {
		expect(fmtLeverageBp(35000, 'pt-BR')).toBe('3,5x');
		expect(fmtLeverageBp(35000, 'en')).toBe('3.5x');
	});
	it('formats the closest liquidation distance, or a dash when never in a position', () => {
		expect(fmtLiqDistance(425, 'pt-BR')).toBe('4,25%');
		expect(fmtLiqDistance(425, 'en')).toBe('4.25%');
		expect(fmtLiqDistance(-1, 'en')).toBe('—');
	});
});

describe('stopAdvice', () => {
	it('gives the stop, the liquidation price and the leverage that keeps the stop in front', () => {
		expect(stopAdvice(perp, 'pt-BR')).toEqual({ stop: '95,00', liq: '95,96', lev: 16, used: 20 });
	});
	it('is null when the stop was in front of liquidation, and for option rows', () => {
		expect(stopAdvice({ ...perp, stopBeyondLiq: false, safeLeverage: null }, 'en')).toBeNull();
		expect(stopAdvice({ ...perp, series: 2 }, 'en')).toBeNull();
	});
});

describe('findings with the safe leverage', () => {
	const liq: EngineFinding = { code: 'liquidated', tone: 'bad', step: 64, count: 16, price: '95.96', requested: '0', amount: '50' };
	it('names the leverage that would have kept the stop in front', () => {
		expect(describeFinding(liq, 'pt-BR')).toContain('com até 16x de alavancagem');
		expect(describeFinding(liq, 'en')).toContain('up to 16x leverage');
	});
	it('falls back to the generic text without one', () => {
		expect(describeFinding({ ...liq, count: 0 }, 'en')).toContain('Lower leverage would have put the stop before liquidation');
	});
	it('describes a stop beyond liquidation with its prices', () => {
		const f: EngineFinding = { code: 'stop_beyond_liq', tone: 'bad', step: 61, count: 16, price: '95', requested: '95.96', amount: '0' };
		expect(describeFinding(f, 'pt-BR')).toContain('O stop em 95,00 (entrada na barra 61) ficou além da liquidação em 95,96');
		expect(describeFinding(f, 'en')).toContain('Up to 16x leverage');
	});
});

describe('theta and IV findings point to the Options lesson', () => {
	it('describes theta decay and an IV drop with the amount lost', () => {
		const theta: EngineFinding = { code: 'theta_decay', tone: 'warn', step: 0, count: 0, price: '0', requested: '0', amount: '1234.5' };
		expect(describeFinding(theta, 'pt-BR')).toContain('theta) custou 1.234,50 cr');
		expect(describeFinding({ ...theta, code: 'iv_crush' }, 'en')).toContain('(vega) cost 1,234.50 cr');
	});
	it('regression: a writer hurt by rising IV is not told about an IV crush or buying with high IV', () => {
		const rise: EngineFinding = { code: 'iv_rise', tone: 'warn', step: 0, count: 0, price: '0', requested: '0', amount: '80' };
		const pt = describeFinding(rise, 'pt-BR');
		expect(pt).toContain('A alta da volatilidade implícita (vega) custou 80,00 cr nas opções vendidas');
		expect(pt).not.toMatch(/queda|Comprar com IV alta/);
		expect(describeFinding(rise, 'en')).not.toMatch(/Falling|Buying when IV/);
	});
	it('regression: a naked write says why it gets no good-process credit (never "limited risk")', () => {
		const w: EngineFinding = { code: 'wrote_options', tone: 'warn', step: 0, count: 0, price: '0', requested: '0', amount: '0' };
		expect(describeFinding(w, 'pt-BR')).toMatch(/a descoberto.*nem pelo prêmio/);
		expect(describeFinding(w, 'pt-BR')).not.toMatch(/risco limitado/);
	});
	it('names the Options lesson', () => {
		expect(describeLesson('options', 'pt-BR')).toMatch(/^Opções II/);
		expect(describeLesson('options', 'en')).toMatch(/^Options II/);
	});
});

describe('describeFindings', () => {
	const liq: EngineFinding = { code: 'liquidated', tone: 'bad', step: 86, count: 9, price: '95.10', requested: '0', amount: '500' };
	const beyond: EngineFinding = { code: 'stop_beyond_liq', tone: 'bad', step: 61, count: 9, price: '89.63', requested: '95.10', amount: '0' };
	it('regression: the safe leverage is said once when the stop finding already names it (m03 seed 303)', () => {
		const texts = describeFindings([liq, beyond], 'pt-BR');
		expect(texts.filter((x) => x.includes('até 9x'))).toHaveLength(1);
		expect(texts[0]).toBe('Liquidação na barra 86 a 95,10: a margem isolada inteira (500,00) foi perdida.');
		expect(texts[1]).toContain('O stop em 89,63');
	});
	it('keeps the advice on the liquidation line when no stop finding carries it', () => {
		expect(describeFindings([liq], 'en')[0]).toContain('up to 9x leverage');
	});
});

describe('revealTexts', () => {
	const csv: PackReveal = {
		asset: 'BTC', pair: 'BTC/USDT', venue: '', periodFrom: '2023-11-14T22:00:00Z', periodTo: '2023-11-19T21:00:00Z',
		source: 'csv-import', transformation: 'prices x 100 / open of bar 60, half-even to 0.01; volume x the inverse', gaps: 0
	};
	it('regression: a CSV pack reveal is in the player language, without the raw source code or an empty venue', () => {
		const r = revealTexts(csv, 'Mercado K');
		expect(r.transform).toBe('preços × 100 / abertura da barra 60, arredondados a 0,01 (meio-par); volume × o inverso');
		expect(r.line).toBe('O mercado Mercado K era BTC/USDT (BTC), de 2023-11-14T22:00:00Z a 2023-11-19T21:00:00Z (CSV importado neste dispositivo).');
		expect(r.line).not.toContain('em —');
		i18n.locale = 'en';
		expect(revealTexts({ ...csv, venue: 'Binance' }, 'K').line).toContain('on Binance');
		i18n.locale = 'pt-BR';
	});
	it('shows a publisher transformation note as written', () => {
		const r = revealTexts({ ...csv, source: 'publisher', transformation: 'rebased to 100' }, 'K');
		expect(r.transform).toBe('rebased to 100');
		expect(r.line).toContain('(publisher)');
	});
});

describe('debrief rows (playtest regressions)', () => {
	const spans: RegimeSpan[] = [
		{ start: 0, bars: 70, label: 'range', direction: 0 },
		{ start: 70, bars: 30, label: 'trend', direction: 1 },
		{ start: 100, bars: 40, label: 'trend', direction: -1 }
	];
	it('regression: the entry regime keeps the trend direction of its span (was "Tendência")', () => {
		expect(entryRegimeName({ ...perp, entryStep: 75, regime: 'trend' }, spans)).toBe('Tendência de alta');
		expect(entryRegimeName({ ...perp, entryStep: 100, regime: 'trend' }, spans)).toBe('Tendência de baixa');
		expect(entryRegimeName({ ...perp, entryStep: 61, regime: 'range' }, spans)).toBe('Lateral');
		expect(entryRegimeName({ ...perp, regime: null }, spans)).toBe('—');
	});
	it('regression: what is still open at the end is its own outcome line, so the lines add up to the net', () => {
		// m06 seed 64: realized 0, fees 9,81, funding -29,10, net +1.292,94: the rest is the open long.
		const wallet = 10000 - 9.81 - 29.1;
		expect(openPnl({ wallet: String(wallet), equity: String(wallet + 1331.85) })).toBeCloseTo(1331.85, 8);
		expect(openPnl({ wallet: '10000', equity: '10000' })).toBe(0);
	});
	it('regression: released headlines are in the timeline; a fast-wire item still early is not', () => {
		const n: NewsView = { step: 80, kind: 'liquidity_outflow', tone: -1, credibility: 90, expectation: 0, actual: -2, early: false };
		const rows = newsTimeline([n, { ...n, step: 95, early: true }], 'pt-BR');
		expect(rows).toEqual([{ step: 80, key: 'n80-liquidity_outflow', text: `📰 ${headline(n, 'pt-BR')}`, tone: 'down' }]);
	});
});

describe('decision records', () => {
	it('counts acted, held and waited decisions', () => {
		expect(
			decisionCounts([
				{ step: 60, kind: 'wait' },
				{ step: 61, kind: 'act' },
				{ step: 62, kind: 'hold' },
				{ step: 63, kind: 'hold' }
			])
		).toEqual({ act: 1, hold: 2, wait: 1 });
		expect(decisionCounts([])).toEqual({ act: 0, hold: 0, wait: 0 });
	});
});

describe('scoring v11 risk findings', () => {
	const base: EngineFinding = { code: 'liq_buffer', tone: 'warn', step: 0, count: 230, price: '0', requested: '0', amount: '0' };
	it('says how close liquidation came, in the locale', () => {
		expect(describeFinding(base, 'pt-BR')).toContain('2,3%');
		expect(describeFinding(base, 'en')).toContain('2.3%');
	});
	it('names the drawdown budget and the drawdown', () => {
		const f: EngineFinding = { ...base, code: 'drawdown_budget', count: 1000, amount: '1200' };
		expect(describeFinding(f, 'pt-BR')).toContain('10%');
		expect(describeFinding(f, 'pt-BR')).toContain('1.200,00');
	});
	it('names the leverage, the stop and the liquidation price of a stop too close to liquidation', () => {
		const f: EngineFinding = { ...base, code: 'leverage_vs_stop', step: 61, count: 12, price: '96', requested: '92.6' };
		const text = describeFinding(f, 'pt-BR');
		expect(text).toContain('12x');
		expect(text).toContain('92,60');
		expect(text).toContain('96,00');
	});
});
