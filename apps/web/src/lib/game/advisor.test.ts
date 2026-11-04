import { describe, expect, it } from 'vitest';
import type { AdviceReasonCode } from '#lib/engine/types.ts';
import { pctFromBp, reasonLine, voteMark } from './advisor.ts';
import { i18n } from './i18n.svelte.ts';

// Every reason code the engine can emit (engine/src/c_api.cpp advice_reason_name).
const CODES: AdviceReasonCode[] = [
	'regime_trend_up', 'regime_trend_down', 'regime_range', 'rsi_extreme', 'band_fade', 'macd_momentum', 'pattern',
	'news_priced', 'news_early', 'funding_crowded', 'low_vol_squeeze'
];

describe('advisor display', () => {
	it('every advice reason reads as plain language in both locales', () => {
		for (const locale of ['pt-BR', 'en'] as const) {
			i18n.locale = locale;
			for (const code of CODES) {
				const line = reasonLine({ code, vote: 1, ref: 80 }, [], locale);
				expect(line, `${locale} ${code}`).not.toMatch(/adv\.r\.|\{|_/);
			}
		}
		i18n.locale = 'pt-BR';
	});

	it('names the pattern at the referenced bar and the crowded side', () => {
		i18n.locale = 'en';
		const line = reasonLine({ code: 'pattern', vote: 2, ref: 81 }, [{ step: 81, id: 'hammer', bars: 1, bias: 1 }], 'en');
		expect(line).toContain('81');
		expect(line).not.toContain('?');
		expect(reasonLine({ code: 'funding_crowded', vote: 0, ref: 1 }, [], 'en')).toContain('longs');
		expect(reasonLine({ code: 'funding_crowded', vote: 0, ref: -1 }, [], 'en')).toContain('shorts');
		expect(reasonLine({ code: 'news_priced', vote: 0, ref: 93 }, [], 'en')).toContain('93');
		i18n.locale = 'pt-BR';
	});

	it('marks which way each reason voted', () => {
		expect(voteMark(1)).toBe('▲');
		expect(voteMark(-2)).toBe('▼▼');
		expect(voteMark(0)).toBe('·');
		expect(pctFromBp(5800)).toBe(58);
	});
});
