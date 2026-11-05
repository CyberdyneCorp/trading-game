import { describe, expect, it } from 'vitest';
import { i18n, packReasonText, reasonText, tCount } from './i18n.svelte.ts';

// Every reason code the engine can emit (engine/src/c_api.cpp reason_name), except "none".
const CODES = [
	'insufficient_margin', 'leverage_above_cap', 'invalid_tick', 'invalid_quantity', 'unsupported_order_type',
	'reduce_only_would_increase', 'run_bankrupt', 'run_finished', 'insufficient_capacity', 'liquidation',
	'position_closed', 'user_cancelled', 'unknown_order', 'invalid_sequence', 'wrong_decision_step',
	'numeric_overflow', 'invalid_leverage', 'position_open', 'stop_beyond_liquidation', 'short_not_allowed',
	'expired', 'invalid_bracket', 'oco_sibling_filled', 'series_not_listed', 'short_options_not_allowed', 'no_bid',
	'margin_call', 'margin_below_initial', 'no_question_due', 'invalid_plan', 'no_advice_left', 'order_moved'
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
	it('regression: one bar is singular ("em 1 barra", not "em 1 barras")', () => {
		expect(tCount('rail.fundingIn', 1)).toBe('em 1 barra');
		expect(tCount('rail.fundingIn', 3)).toBe('em 3 barras');
		i18n.locale = 'en';
		expect(tCount('rail.fundingIn', 1)).toBe('in 1 bar');
		expect(tCount('opt.expiry', 1, { step: 96 })).toBe('Expiry at bar 96 (1 bar)');
		i18n.locale = 'pt-BR';
	});
});

describe('packReasonText', () => {
	it('regression: every CSV build refusal reads as words (csv_invalid was shown raw)', () => {
		for (const code of ['csv_invalid', 'invalid_rules', 'masking_failed', 'invalid_meta', 'invalid_seed']) {
			expect(packReasonText(code)).not.toMatch(/_/);
		}
	});
});
