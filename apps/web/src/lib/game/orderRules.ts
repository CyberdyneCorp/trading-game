// Order rules of execution policy v18 and the trailing stop, as the terminal shows them
// (add-practice-and-advanced-strategies D6, D7, D10). The engine decides every fill, expiry and
// link; these helpers only choose what the ticket sends and how the orders panel and chart name it.

import type { Decimal, OrderSideName, OrderTypeName, OrderView, TimeInForceName } from '#lib/engine/types.ts';
import { fmtPct, fmtPrice } from './format.ts';
import { toRaw, trailTrigger } from './stops.ts';

export { toRaw, trailTrigger };
import { t, type Locale } from './i18n.svelte.ts';

// ---- time in force (D6) ------------------------------------------------------------------------

/** What the ticket offers: "day" is bars:N with N = one day of bars. */
export type TifChoice = 'gtc' | 'day' | 'bars' | 'ioc' | 'fok';

export const TIF_CHOICES: readonly TifChoice[] = ['gtc', 'day', 'bars', 'ioc', 'fok'];

/** The bars in one day at this market's interval (86,400 / stepSeconds), at least 1. */
export function dayBars(stepSeconds: number): number {
	return stepSeconds > 0 ? Math.max(1, Math.round(86_400 / stepSeconds)) : 1;
}

/** Bars an order placed now can still work on: up to the run's last bar. */
export function barsLeft(game: { totalBars: number; step: number }): number {
	return Math.max(0, game.totalBars - 1 - game.step);
}

/** Why a choice cannot go with this order type (the engine would refuse it), or null. */
export type TifBlock = 'needs_limit' | 'market_immediate';

export function tifBlock(choice: TifChoice, type: OrderTypeName | 'market' | 'limit'): TifBlock | null {
	if (choice === 'ioc' || choice === 'fok') return type === 'limit' ? null : 'needs_limit';
	if (choice === 'day' || choice === 'bars') return type === 'market' ? 'market_immediate' : null;
	return null;
}

export type TifOrder = { tif: TimeInForceName; tifBars?: number };

/**
 * The time in force a ticket sends, or why it cannot: N bars must be between 1 and the bars left
 * ("day" longer than the run is cut to what is left).
 */
export function tifOrder(choice: TifChoice, barsInput: number, stepSeconds: number, left: number): TifOrder | { error: 'bars_range' } {
	if (choice === 'gtc' || choice === 'ioc' || choice === 'fok') return { tif: choice };
	const n = choice === 'day' ? Math.min(dayBars(stepSeconds), left) : Math.floor(barsInput);
	if (!(n >= 1) || n > left) return { error: 'bars_range' };
	return { tif: 'bars', tifBars: n };
}

/** The orders panel's time-in-force cell: the kind and, for bars/ioc/fok, the last bar it can execute on. */
export function tifCell(o: Pick<OrderView, 'tif' | 'tifBars' | 'expireStep'>): { kind: TimeInForceName; until: number | null } {
	const kind = o.tif ?? 'gtc';
	return { kind, until: kind === 'gtc' || !o.expireStep ? null : o.expireStep };
}

// ---- player-built OCO links (D7) -----------------------------------------------------------------

/** Why two working orders cannot be linked (the engine's invalid_oco), or null. */
export type LinkProblem = 'same' | 'child' | 'linked';

export function linkProblem(a: Pick<OrderView, 'id' | 'parentId' | 'linkGroup'>, b: Pick<OrderView, 'id' | 'parentId' | 'linkGroup'>): LinkProblem | null {
	if (a.id === b.id) return 'same';
	if (a.parentId || b.parentId) return 'child';
	if (a.linkGroup || b.linkGroup) return 'linked';
	return null;
}

/** The other working order of a link, if it still works. */
export function linkPartner(o: Pick<OrderView, 'id' | 'linkGroup'>, orders: readonly OrderView[]): OrderView | undefined {
	if (!o.linkGroup) return undefined;
	return orders.find((x) => x.id !== o.id && x.linkGroup === o.linkGroup);
}

/** The link mark on an order and its chart line: "⛓#3" names the partner. */
export function linkMark(o: Pick<OrderView, 'id' | 'linkGroup'>, orders: readonly OrderView[]): string | null {
	const p = linkPartner(o, orders);
	return p ? `⛓#${p.id}` : null;
}

// ---- trailing stop (D10) -------------------------------------------------------------------------

/**
 * The close the trigger follows: the highest close (a sell stop, protecting a long) or the lowest (a
 * buy stop) from the placement bar to the current one; only bars the player has seen.
 */
export function bestClose(closes: readonly number[], placedStep: number, exit: OrderSideName): { step: number; close: number } | null {
	let best: { step: number; close: number } | null = null;
	for (let i = placedStep; i < closes.length; i++) {
		const c = closes[i];
		if (!Number.isFinite(c)) continue;
		const better = best == null || (exit === 'sell' ? c > best.close : c < best.close);
		if (better) best = { step: i, close: c };
	}
	return best;
}

/** The chart's trailing label: the rule and the trigger, "TRAIL 2% · 101,96" or "TRAIL 1,50 · 101,96". */
export function trailLabel(o: Pick<OrderView, 'trailDistance' | 'trailRate' | 'price'>, locale: Locale): string {
	const rate = Number(o.trailRate ?? '0');
	const rule = rate > 0 ? fmtPct(rate, locale, rate * 100 >= 1 && Number.isInteger(rate * 100) ? 0 : 2) : fmtPrice(o.trailDistance, locale);
	return `TRAIL ${rule} · ${fmtPrice(o.price, locale)}`;
}

/**
 * What a trailing line's label adds after its id: the best close the trigger follows (" · best close
 * 101,90"), or nothing. The chart's marker on that bar carries no text of its own: it collided with the
 * ticket's ENTRY? label (wave 1 playtest).
 */
export function trailFollows(o: Pick<OrderView, 'type' | 'placedStep' | 'side'>, closes: readonly number[], locale: Locale): string {
	const best = o.type === 'trailing_stop' ? bestClose(closes, o.placedStep, o.side) : null;
	return best ? ` · ${t('chart.bestClose', { price: fmtPrice(String(best.close), locale) })}` : '';
}

/** A planned trailing stop (D10): recorded with the plan, sent only when the player clicks after the fill. */
export interface PlannedTrail {
	mode: 'price' | 'pct';
	/** The distance (price mode) or the percent (pct mode) as typed. */
	value: string;
}

/** The trailing order a planned trail becomes: a fixed distance on the tick, or the engine rate. */
export function plannedTrailOrder(p: PlannedTrail, tick: number, toRate: (pct: number) => string | null, parse: (s: string) => number): { trail?: string; trailRate?: string } | null {
	const v = parse(p.value);
	if (!(v > 0)) return null;
	if (p.mode === 'pct') {
		const rate = toRate(v);
		return rate ? { trailRate: rate } : null;
	}
	const ticks = Math.round(v / tick);
	if (ticks < 1) return null;
	const decimals = Math.max(0, Math.round(-Math.log10(tick)));
	return { trail: (ticks * tick).toFixed(decimals) };
}
