// Display helpers for the engine's advice (spec advisor "Advice in the terminal and the debrief").
// The engine decides everything; this only turns reason codes into localized lines.

import type { AdviceReasonView, PatternHitView } from '#lib/engine/types.ts';
import { patternName } from './candlePatterns.ts';
import { t, type Locale, type MessageKey } from './i18n.svelte.ts';

/** ▲ long, ▼ short (doubled for a weight-2 vote), · neither (toward wait, or a warning). */
export function voteMark(vote: number): string {
	if (vote === 0) return '·';
	return (vote > 0 ? '▲' : '▼').repeat(Math.min(2, Math.abs(vote)));
}

/** Localized line for one reason; `patterns` resolves a pattern reason's bar to its name. */
export function reasonLine(r: AdviceReasonView, patterns: PatternHitView[], locale: Locale): string {
	const key = `adv.r.${r.code}` as MessageKey;
	if (r.code === 'pattern') {
		const hit = patterns.find((p) => p.step === r.ref && p.bias === Math.sign(r.vote));
		return t(key, { name: hit ? patternName(hit.id, locale) : '?', bar: r.ref });
	}
	if (r.code === 'funding_crowded') return t(key, { who: t(r.ref > 0 ? 'rail.longs' : 'rail.shorts') });
	return t(key, { bar: r.ref });
}

/** Percent with no decimals from basis points. */
export const pctFromBp = (bp: number): number => Math.round(bp / 100);
