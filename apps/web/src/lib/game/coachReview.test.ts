import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import type { CoachMoment, CoachMomentCode, CoachReview, EngineFinding, GameState } from '#lib/engine/types.ts';
import { findingRetryable, mistakeName, mistakeOutcome, momentKey, momentText, REPEAT_WINDOW, retryable, stateAt } from './coachReview.ts';
import { i18n, t } from './i18n.svelte.ts';
import { lessonMeta } from '#lib/content/academy.ts';
import { termById } from '#lib/content/glossary.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';

const CODES: CoachMomentCode[] = [
	'entry_without_stop',
	'stop_moved_away',
	'added_against_structure',
	'risk_above_budget',
	'held_into_release_unhedged',
	'revenge_trade',
	'fomo_entry',
	'averaging_down',
	'risk_limit_exceeded',
	'cut_at_invalidation',
	'stop_to_breakeven_after_1r',
	'partial_at_target',
	'stayed_flat_before_release'
];

const moment = (code: CoachMomentCode, evidence: CoachMoment['evidence'] = {}, step = 84): CoachMoment => ({
	step,
	code,
	tone: ['cut_at_invalidation', 'stop_to_breakeven_after_1r', 'partial_at_target', 'stayed_flat_before_release'].includes(code) ? 'good' : 'warn',
	evidence,
	lesson: 'risk-sizing',
	glossary: 'stop-loss'
});

afterEach(() => {
	i18n.locale = 'pt-BR';
});

describe('coach moment texts (design D17)', () => {
	it('every code and variant has a text in both locales, with no placeholder left', () => {
		const variants: CoachMoment[] = [
			...CODES.map((c) => moment(c, { stop: '95', qty: '10', planned: false, close: '99', sma: '100' })),
			moment('added_against_structure', { close: '101', sma: '100' }),
			moment('held_into_release_unhedged', { releaseStep: 90, limit: 'vega', limitBp: 150, valueBp: 210 }),
			moment('revenge_trade', { planned: true }),
			moment('fomo_entry', { stop: '0' }),
			moment('averaging_down', { stop: '0' })
		];
		for (const locale of ['pt-BR', 'en'] as const) {
			i18n.locale = locale;
			for (const m of variants) {
				const key = momentKey(m);
				expect(t(key), key).not.toBe(key);
				expect(momentText(m, locale)).not.toMatch(/\{\w+\}/);
			}
		}
	});

	it('picks the variant from the evidence', () => {
		expect(momentKey(moment('added_against_structure', { close: '101', sma: '100' }))).toBe('coachReview.moment.added_against_structure.short');
		expect(momentKey(moment('held_into_release_unhedged', { releaseStep: 90 }))).toBe('coachReview.moment.held_into_release_unhedged.vega');
		expect(momentKey(moment('held_into_release_unhedged', { releaseStep: 90, qty: '10' }))).toBe('coachReview.moment.held_into_release_unhedged');
		expect(momentKey(moment('averaging_down', { stop: '97.5' }))).toBe('coachReview.moment.averaging_down');
		expect(momentKey(moment('fomo_entry', {}))).toBe('coachReview.moment.fomo_entry.nostop');
	});

	it('states the evidence numbers in the locale', () => {
		i18n.locale = 'en';
		const m = moment('revenge_trade', { trade: 1, exitStep: 83, loss: '112.81024645', sizeRatioBp: 20000, planned: false }, 83);
		expect(momentText(m, 'en')).toBe(
			'Bar 83: right after trade 1 closed at a loss of 112.81 cr on bar 83, a new entry came, with no plan, at 2.00× its size. After a loss, wait for the setup to form again.'
		);
		i18n.locale = 'pt-BR';
		expect(momentText(m, 'pt-BR')).toContain('perda de 112,81 cr');
	});
});

describe('the chart at a moment (no future drawn)', () => {
	const state = {
		step: 179,
		bars: Array.from({ length: 180 }, (_, i) => [`${i}`, `${i}`, `${i}`, `${i}`, '1', false]),
		indicators: {
			version: 1,
			sma20: Array(180).fill('1'),
			ema50: Array(180).fill('1'),
			rsi14: Array(180).fill('1'),
			atr14: Array(180).fill('1'),
			vwap: Array(180).fill('1'),
			bbUpper: Array(180).fill('1'),
			bbLower: Array(180).fill('1'),
			macd: Array(180).fill('1'),
			macdSignal: Array(180).fill('1'),
			macdHist: Array(180).fill('1'),
			plusDi14: Array(180).fill('1'),
			minusDi14: Array(180).fill('1'),
			adx14: Array(180).fill('1'),
			patternVersion: 1,
			patterns: [
				{ step: 80, id: 'hammer', bars: 1, bias: 'bullish' },
				{ step: 100, id: 'hammer', bars: 1, bias: 'bullish' }
			]
		},
		events: [{ step: 70 }, { step: 84 }, { step: 120 }],
		news: [{ step: 60 }, { step: 130 }],
		orders: [{ id: 1 }],
		position: { qty: '10', entry: '100', margin: '1', side: 'long' },
		upcoming: [{ step: 150 }],
		calendar: [150]
	} as unknown as GameState;

	it('Future hidden at a moment: at step 84 no bar, indicator, pattern, event or headline after it', () => {
		const cut = stateAt(state, 84);
		expect(cut.step).toBe(84);
		expect(cut.bars).toHaveLength(85);
		expect(cut.bars.at(-1)?.[3]).toBe('84');
		expect(cut.indicators.sma20).toHaveLength(85);
		expect(cut.indicators.adx14).toHaveLength(85);
		expect(cut.indicators.patterns.map((p) => p.step)).toEqual([80]);
		expect(cut.events.map((e) => e.step)).toEqual([70, 84]);
		expect(cut.news.map((n) => n.step)).toEqual([60]);
		expect(cut.orders).toEqual([]);
		expect(cut.position.qty).toBe('0'); // the final position is not what bar 84 knew
		expect(cut.upcoming).toEqual([]);
		expect(state.bars).toHaveLength(180); // the run's own state is untouched
	});
});

describe('mistake replay (design D18)', () => {
	const f = (code: string, step: number, tone: EngineFinding['tone'] = 'warn') => ({ code, step, tone }) as EngineFinding;

	it('Avoided: no averaging_down from step 90 to 95; repeated when one is at 95', () => {
		expect(mistakeOutcome('averaging_down', 90, { moments: [], findings: [] })).toBe('avoided');
		expect(mistakeOutcome('averaging_down', 90, { moments: [], findings: [f('averaging_down', 96)] })).toBe('avoided');
		expect(mistakeOutcome('averaging_down', 90, { moments: [], findings: [f('averaging_down', 89)] })).toBe('avoided');
		expect(mistakeOutcome('averaging_down', 90, { moments: [], findings: [f('averaging_down', 90 + REPEAT_WINDOW)] })).toBe('repeated');
		expect(mistakeOutcome('averaging_down', 90, { moments: [moment('averaging_down', {}, 92)], findings: [] })).toBe('repeated');
	});

	it('maps a finding to the coach code naming the same mistake', () => {
		expect(mistakeOutcome('no_stop', 70, { moments: [moment('entry_without_stop', {}, 71)], findings: [] })).toBe('repeated');
		expect(mistakeOutcome('oversized', 70, { moments: [moment('risk_above_budget', {}, 70)], findings: [] })).toBe('repeated');
		expect(mistakeOutcome('entry_without_stop', 70, { moments: [], findings: [f('no_stop', 0)] })).toBe('avoided');
	});

	it('offers a retry on warnings with a step only, and names the moment without its outcome', () => {
		expect(retryable(moment('revenge_trade'))).toBe(true);
		expect(retryable(moment('cut_at_invalidation'))).toBe(false);
		expect(findingRetryable(f('revenge_trade', 83), 60)).toBe(true);
		expect(findingRetryable(f('no_stop', 0), 60)).toBe(false);
		expect(findingRetryable(f('good_process_win', 83, 'good'), 60)).toBe(false);
		expect(mistakeName('revenge_trade')).toBe('mistake.name.revenge_trade');
		expect(mistakeName('gap_slippage')).toBe('mistake.name.other');
		for (const code of [...CODES.slice(0, 9), 'oversized', 'leverage_vs_stop', 'other']) {
			const key = mistakeName(code);
			for (const locale of ['pt-BR', 'en'] as const) {
				i18n.locale = locale;
				expect(t(key)).not.toBe(key);
			}
		}
	});
});

// The engine's coach_lesson / coach_glossary (pinned in engine/tests/test_coach_review.cpp).
const ENGINE_LINKS: Record<CoachMomentCode, [string, string]> = {
	entry_without_stop: ['risk-sizing', 'stop-loss'],
	stop_moved_away: ['mind-plan', 'stop-loss'],
	added_against_structure: ['structure-zones', 'trend'],
	risk_above_budget: ['risk-sizing', 'risk-budget'],
	held_into_release_unhedged: ['events-surprise', 'gap'],
	revenge_trade: ['mind-biases', 'revenge-trading'],
	fomo_entry: ['mind-biases', 'fomo'],
	averaging_down: ['mind-biases', 'averaging-down'],
	risk_limit_exceeded: ['options-greeks', 'risk-limit'],
	cut_at_invalidation: ['mind-plan', 'invalidation'],
	stop_to_breakeven_after_1r: ['management-scaling', 'breakeven-stop'],
	partial_at_target: ['management-scaling', 'partial-take-profit'],
	stayed_flat_before_release: ['events-surprise', 'surprise']
};

describe('coach links', () => {
	it('every lesson and glossary id the engine names exists in the content', () => {
		for (const [code, [lesson, term]] of Object.entries(ENGINE_LINKS)) {
			expect(lessonMeta(lesson), `${code}: ${lesson}`).not.toBeNull();
			expect(termById(term), `${code}: ${term}`).not.toBeNull();
		}
	});
});

describe.skipIf(!wasmBuilt)('coach review through the browser engine (WASM)', () => {
	let engine: NodeEngine;
	beforeAll(async () => {
		engine = await loadEngine();
	});

	it('refuses an active run, then reviews the finished one with real lesson and glossary ids', () => {
		const run = engine.createRun('m23-second-chance', 3);
		expect(engine.call('tl_coach_review', [run], ['number'])).toEqual({ error: 'run_active' });
		engine.call('tl_place_order', [run, 1, 0, 0, '10', '', 0], ['number', 'number', 'number', 'number', 'string', 'string', 'number']);
		engine.advance(run, 1000);
		const review = engine.call<CoachReview>('tl_coach_review', [run], ['number']);
		expect(review.mission).toBe('m23-second-chance');
		expect(review.moments.map((m) => m.code)).toContain('entry_without_stop');
		for (const m of review.moments) {
			expect(lessonMeta(m.lesson), m.lesson).not.toBeNull();
			expect(termById(m.glossary), m.glossary).not.toBeNull();
			expect(momentText(m, 'en')).not.toMatch(/\{\w+\}/);
		}
	});
});
