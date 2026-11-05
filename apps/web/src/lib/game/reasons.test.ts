import { describe, expect, it } from 'vitest';
import { i18n, packReasonText, reasonText, t, tCount } from './i18n.svelte.ts';

// Every reason code the engine can emit (engine/idl/tradelab.idl Reason), except "none".
const CODES = [
	'insufficient_margin', 'leverage_above_cap', 'invalid_tick', 'invalid_quantity', 'unsupported_order_type',
	'reduce_only_would_increase', 'run_bankrupt', 'run_finished', 'insufficient_capacity', 'liquidation',
	'position_closed', 'user_cancelled', 'unknown_order', 'invalid_sequence', 'wrong_decision_step',
	'numeric_overflow', 'invalid_leverage', 'position_open', 'stop_beyond_liquidation', 'short_not_allowed',
	'expired', 'invalid_bracket', 'oco_sibling_filled', 'series_not_listed', 'short_options_not_allowed', 'no_bid',
	'margin_call', 'margin_below_initial', 'no_question_due', 'invalid_plan', 'no_advice_left', 'order_moved',
	'leverage_above_tier', 'series_settling', 'unknown_question', 'already_answered', 'invalid_answer'
];

describe('reasonText', () => {
	it('regression: every engine reason code reads as plain language in both locales (was the raw code)', () => {
		for (const locale of ['pt-BR', 'en'] as const) {
			i18n.locale = locale;
			for (const code of CODES) expect(reasonText(code), `${locale} ${code}`).not.toMatch(/_/);
		}
		i18n.locale = 'pt-BR';
		expect(reasonText('expired')).toBe('expirada');
	});

	it('falls back to the code for anything unknown', () => {
		expect(reasonText('something_new')).toBe('something_new');
	});
});

describe('tCount', () => {
	// Playtest: the CSV import report read "2000 barras".
	it('regression: writes the count in the locale ("2.000 barras", "2,000 bars")', () => {
		expect(tCount('csv.rows', 2000)).toBe('2.000 barras');
		expect(tCount('csv.rows', 1)).toBe('1 barra');
		expect(tCount('csv.more', 1200)).toBe('… e mais 1.200');
		i18n.locale = 'en';
		expect(tCount('csv.rows', 2000)).toBe('2,000 bars');
		i18n.locale = 'pt-BR';
	});

	it('regression: one bar is singular ("em 1 barra", not "em 1 barras")', () => {
		expect(tCount('rail.fundingIn', 1)).toBe('em 1 barra');
		expect(tCount('rail.fundingIn', 3)).toBe('em 3 barras');
		i18n.locale = 'en';
		expect(tCount('rail.fundingIn', 1)).toBe('in 1 bar');
		expect(tCount('opt.expiry', 1, { step: 96 })).toBe('Expiry at bar 96 (1 bar)');
		i18n.locale = 'pt-BR';
	});

	it('regression: debrief and CSV counts are pluralized, never "(s)"', () => {
		expect(tCount('debrief.calibrationLine', 5, { brier: '0,210' })).toBe('5 perguntas resolvidas · Brier 0,210');
		expect(tCount('debrief.calibrationLine', 1, { brier: '0,210' })).toBe('1 pergunta resolvida · Brier 0,210');
		expect(tCount('csv.gapLine', 1, { line: 7 })).toBe('linha 7: 1 barra ausente');
		expect(tCount('csv.gapLine', 2, { line: 7 })).toBe('linha 7: 2 barras ausentes');
		i18n.locale = 'en';
		expect(tCount('debrief.calibrationLine', 5, { brier: '0.210' })).toBe('5 resolved questions · Brier 0.210');
		expect(tCount('debrief.revealGaps', 1)).toBe('1 gap in the data was kept as a jump.');
		i18n.locale = 'pt-BR';
		for (const key of ['debrief.calibrationLine', 'csv.errors', 'csv.gaps', 'csv.gapLine', 'debrief.revealGaps'] as const) {
			for (const n of [1, 3]) expect(tCount(key, n, { line: 1, brier: '0' })).not.toMatch(/\(s\)|\(ões\)|\(ies\)/);
		}
	});
});

describe('packReasonText', () => {
	it('regression: every CSV build refusal reads as words (csv_invalid was shown raw)', () => {
		for (const code of ['csv_invalid', 'invalid_rules', 'masking_failed', 'invalid_meta', 'invalid_seed']) {
			expect(packReasonText(code)).not.toMatch(/_/);
		}
	});
});

describe('debrief Greek note', () => {
	it('regression: describes the full revaluation (scoring v8), not the old Taylor method', () => {
		// Before: "gregas do início de cada intervalo ... efeitos de ordem maior" after v8 changed the method.
		expect(t('debrief.greeksNote')).toMatch(/reavaliação completa/);
		expect(t('debrief.greeksNote')).toMatch(/resíduo é o spread pago/);
		expect(t('debrief.greeksNote')).not.toMatch(/ordem maior|início de cada intervalo/);
		i18n.locale = 'en';
		expect(t('debrief.greeksNote')).toMatch(/full revaluation/);
		expect(t('debrief.greeksNote')).not.toMatch(/higher-order|start of each interval/);
		i18n.locale = 'pt-BR';
	});
});
