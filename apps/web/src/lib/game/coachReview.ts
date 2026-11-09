// Coach mode and mistake replay (add-practice-and-advanced-strategies design D17, D18). The engine's
// tl_coach_review gives the moments of a finished run, each read from what was visible at its step;
// this module turns them into texts (numbers from the moment's evidence only), cuts the run's state to
// a moment's bar so the chart draws no future, and, after a mistake retry, says whether the mistake
// was repeated or avoided. The desktop ports the texts and the comparison.

import type { CoachMoment, CoachMomentCode, EngineFinding, GameState } from '#lib/engine/types.ts';
import { fmtNum, fmtPrice, fmtQty } from './format.ts';
import { t, type Locale, type MessageKey } from './i18n.svelte.ts';

const LIMITS: Record<Locale, Record<string, string>> = {
	'pt-BR': { delta: 'delta', gamma: 'gama', vega: 'vega', theta: 'theta' },
	en: { delta: 'delta', gamma: 'gamma', vega: 'vega', theta: 'theta' }
};

const pct = (bp: number, locale: Locale) => `${fmtNum(bp / 100, locale, 2)}%`;
const r = (bp: number, locale: Locale) => fmtNum(bp / 10_000, locale, 2);

/** The text parameters of a moment: its step and the evidence formatted for the locale. */
export function momentParams(m: CoachMoment, locale: Locale): Record<string, string | number> {
	const e = m.evidence;
	const price = (v?: string) => (v == null ? '' : fmtPrice(v, locale));
	return {
		step: m.step,
		qty: e.qty == null ? '' : fmtQty(e.qty, locale),
		entry: price(e.entry),
		close: price(e.close),
		sma: price(e.sma),
		level: price(e.level),
		stop: price(e.stop),
		from: price(e.from),
		to: price(e.to),
		price: price(e.price),
		risk: price(e.risk),
		budget: price(e.budget),
		loss: price(e.loss),
		plannedRisk: price(e.plannedRisk),
		trade: e.trade ?? '',
		exit: e.exitStep ?? '',
		ratio: e.sizeRatioBp == null ? '' : fmtNum(e.sizeRatioBp / 10_000, locale, 2),
		closes: e.closes ?? '',
		ext: e.extensionAtr == null ? '' : fmtNum(e.extensionAtr / 100, locale, 2),
		avgBefore: price(e.avgBefore),
		avgAfter: price(e.avgAfter),
		riskBefore: price(e.riskBefore),
		riskAfter: price(e.riskAfter),
		planRisk: price(e.planRisk),
		release: e.releaseStep ?? '',
		limit: e.limit ? LIMITS[locale][e.limit] : '',
		limitPct: e.limitBp == null ? '' : pct(e.limitBp, locale),
		valuePct: e.valueBp == null ? '' : pct(e.valueBp, locale),
		r: e.profitRBp == null ? '' : r(e.profitRBp, locale)
	};
}

/** The i18n key of a moment's text: one per code, with a variant where the evidence changes the sentence. */
export function momentKey(m: CoachMoment): MessageKey {
	const e = m.evidence;
	if (m.code === 'added_against_structure' && e.close != null && e.sma != null && Number(e.close) > Number(e.sma)) {
		return 'coachReview.moment.added_against_structure.short';
	}
	if (m.code === 'held_into_release_unhedged' && e.qty == null) return 'coachReview.moment.held_into_release_unhedged.vega';
	if (m.code === 'revenge_trade' && e.planned) return 'coachReview.moment.revenge_trade.planned';
	if ((m.code === 'averaging_down' || m.code === 'fomo_entry') && !(Number(e.stop ?? '0') > 0))
		return `coachReview.moment.${m.code}.nostop` as MessageKey;
	return `coachReview.moment.${m.code}` as MessageKey;
}

/** A moment's text in the shown locale. */
export function momentText(m: CoachMoment, locale: Locale): string {
	return t(momentKey(m), momentParams(m, locale));
}

/**
 * The run's state as of a moment's bar: the bars, indicators, patterns, events and news up to it, and
 * none of the final orders or position (which the moment's bar did not know). The chart then draws no
 * bar after the moment and hatches the rest as the unknown future.
 */
export function stateAt(s: GameState, step: number): GameState {
	const upTo = <T>(a: T[]) => a.slice(0, step + 1);
	const ind = s.indicators;
	return {
		...s,
		step,
		bars: upTo(s.bars),
		indicators: {
			...ind,
			sma20: upTo(ind.sma20),
			ema50: upTo(ind.ema50),
			rsi14: upTo(ind.rsi14),
			atr14: upTo(ind.atr14),
			vwap: upTo(ind.vwap),
			bbUpper: upTo(ind.bbUpper),
			bbLower: upTo(ind.bbLower),
			macd: upTo(ind.macd),
			macdSignal: upTo(ind.macdSignal),
			macdHist: upTo(ind.macdHist),
			plusDi14: upTo(ind.plusDi14),
			minusDi14: upTo(ind.minusDi14),
			adx14: upTo(ind.adx14),
			patterns: ind.patterns.filter((p) => p.step <= step)
		},
		events: s.events.filter((e) => e.step <= step),
		news: s.news.filter((n) => n.step <= step),
		orders: [],
		position: { ...s.position, qty: '0', entry: '0' },
		account: { ...s.account, liquidationPrice: null },
		upcoming: [],
		calendar: []
	};
}

/** A warning moment can be retried from its step (design D18). */
export const retryable = (m: { tone: string }): boolean => m.tone === 'warn';

/** A debrief finding is offered "Retry from here" when it has a step and a warning tone. */
export const findingRetryable = (f: EngineFinding, warmupBars: number): boolean => f.tone === 'warn' && f.step >= warmupBars;

/** The coach codes that name the same mistake as a debrief finding's code (bias and limit findings). */
const SAME: Partial<Record<string, CoachMomentCode>> = {
	revenge_trade: 'revenge_trade',
	fomo_entry: 'fomo_entry',
	averaging_down: 'averaging_down',
	risk_limit_exceeded: 'risk_limit_exceeded',
	no_stop: 'entry_without_stop',
	oversized: 'risk_above_budget'
};

/** Steps of a retry where the original mistake's code shows again (its coach moments and its findings). */
export const REPEAT_WINDOW = 5;

/**
 * Repeated when the retry has the same code (a coach moment, or a finding naming it) at a step in
 * [step, step + 5]; avoided otherwise.
 */
export function mistakeOutcome(
	code: string,
	step: number,
	retry: {
		moments: readonly CoachMoment[];
		findings: readonly EngineFinding[];
	}
): 'repeated' | 'avoided' {
	const inWindow = (s: number) => s >= step && s <= step + REPEAT_WINDOW;
	const coachCode = SAME[code] ?? code;
	const byMoment = retry.moments.some((m) => m.code === coachCode && inWindow(m.step));
	const byFinding = retry.findings.some((f) => (f.code === code || SAME[f.code] === coachCode) && inWindow(f.step));
	return byMoment || byFinding ? 'repeated' : 'avoided';
}

const NAMED = new Set([
	'entry_without_stop',
	'stop_moved_away',
	'added_against_structure',
	'risk_above_budget',
	'held_into_release_unhedged',
	'revenge_trade',
	'fomo_entry',
	'averaging_down',
	'risk_limit_exceeded',
	'oversized',
	'leverage_vs_stop'
]);

/** The i18n key naming a retried moment (a coach code or a debrief finding code), never what came after it. */
export const mistakeName = (code: string): MessageKey => (NAMED.has(code) ? (`mistake.name.${code}` as MessageKey) : 'mistake.name.other');
