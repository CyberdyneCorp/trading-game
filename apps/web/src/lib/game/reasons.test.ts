import { describe, expect, it } from 'vitest';
import { i18n, reasonText } from './i18n.svelte.ts';

// Every reason code the engine can emit (engine/src/c_api.cpp reason_name), except "none".
const CODES = [
	'insufficient_margin', 'leverage_above_cap', 'invalid_tick', 'invalid_quantity', 'unsupported_order_type',
	'reduce_only_would_increase', 'run_bankrupt', 'run_finished', 'insufficient_capacity', 'liquidation',
	'position_closed', 'user_cancelled', 'unknown_order', 'invalid_sequence', 'wrong_decision_step',
	'numeric_overflow', 'invalid_leverage', 'position_open', 'stop_beyond_liquidation', 'short_not_allowed',
	'expired', 'invalid_bracket', 'oco_sibling_filled', 'series_not_listed', 'short_options_not_allowed', 'no_bid',
	'margin_call', 'margin_below_initial', 'no_question_due', 'invalid_plan'
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
