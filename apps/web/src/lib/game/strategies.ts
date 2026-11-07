// Strategy builder model (add-academy-and-strategies design D3): templates as legs on the listed chain,
// the submission order and the requests for the engine's what-if valuation and combo dry run. Every
// number the builder shows comes from those engine calls; this module only chooses legs.

import type { ComboLegInput, GameState, OptionQuoteView, OrderSideName, WhatIfLegInput } from '#lib/engine/types.ts';
import { qtyToLot } from './format.ts';

export type TemplateId =
	| 'bull_call'
	| 'bear_put'
	| 'covered_call'
	| 'protective_put'
	| 'straddle'
	| 'strangle'
	| 'iron_condor';

/** A combo's structure name: a template, its short version, or free legs. */
export type StrategyId = TemplateId | 'short_straddle' | 'short_strangle' | 'custom';

export const TEMPLATES: readonly TemplateId[] = [
	'bull_call',
	'bear_put',
	'covered_call',
	'protective_put',
	'straddle',
	'strangle',
	'iron_condor'
];

/** Templates whose legs write options (sell to open): disabled when the run does not allow writing. */
const WRITES: ReadonlySet<TemplateId> = new Set(['bull_call', 'bear_put', 'covered_call', 'iron_condor']);
/** Templates offered short (every side flipped). */
const SHORTABLE: ReadonlySet<TemplateId> = new Set(['straddle', 'strangle']);

export const MAX_OPTION_LEGS = 4;

export type LegKind = 'call' | 'put' | 'perp';

export interface BuilderLeg {
	kind: LegKind;
	side: OrderSideName;
	/** Option strike; null for the perpetual. */
	strike: number | null;
	/** Contracts (options) or underlying units (the perpetual: contracts x the option multiplier). */
	qty: number;
}

export function templateWrites(id: TemplateId, short: boolean): boolean {
	return WRITES.has(id) || (short && SHORTABLE.has(id));
}

export function templateShortable(id: TemplateId): boolean {
	return SHORTABLE.has(id);
}

export function strategyOf(id: TemplateId, short: boolean): StrategyId {
	if (short && id === 'straddle') return 'short_straddle';
	if (short && id === 'strangle') return 'short_strangle';
	return id;
}

/** Expiries a combo can be built on now: listed series whose orders would not fall in the settlement window. */
export function expiriesOf(chain: readonly OptionQuoteView[]): number[] {
	return [...new Set(chain.filter((q) => q.listed && !q.settling).map((q) => q.expiryStep))].sort((a, b) => a - b);
}

export function strikesOf(chain: readonly OptionQuoteView[], expiry: number): number[] {
	return [...new Set(chain.filter((q) => q.expiryStep === expiry).map((q) => Number(q.strike)))].sort((a, b) => a - b);
}

export function seriesFor(chain: readonly OptionQuoteView[], expiry: number, kind: 'call' | 'put', strike: number): OptionQuoteView | undefined {
	return chain.find((q) => q.expiryStep === expiry && q.kind === kind && Number(q.strike) === strike);
}

/** The strike nearest to the forward (the lower one on a tie). */
function nearest(strikes: readonly number[], f: number): number {
	return strikes.reduce((best, k) => (Math.abs(k - f) < Math.abs(best - f) ? k : best), strikes[0]);
}

/** Strikes below the forward (descending) and above it (ascending), strictly. */
function around(strikes: readonly number[], f: number): { below: number[]; above: number[] } {
	return { below: strikes.filter((k) => k < f).reverse(), above: strikes.filter((k) => k > f) };
}

const opt = (kind: 'call' | 'put', side: OrderSideName, strike: number | undefined, qty: number): BuilderLeg | null =>
	strike == null ? null : { kind, side, strike, qty };

function rawLegs(id: TemplateId, strikes: readonly number[], f: number, n: number): (BuilderLeg | null)[] {
	const atm = nearest(strikes, f);
	const i = strikes.indexOf(atm);
	const { below, above } = around(strikes, f);
	const perp: BuilderLeg = { kind: 'perp', side: 'buy', strike: null, qty: n };
	switch (id) {
		case 'bull_call':
			return [opt('call', 'buy', atm, n), opt('call', 'sell', strikes[i + 1], n)];
		case 'bear_put':
			return [opt('put', 'buy', atm, n), opt('put', 'sell', strikes[i - 1], n)];
		case 'covered_call':
			return [perp, opt('call', 'sell', above[0], n)];
		case 'protective_put':
			return [perp, opt('put', 'buy', strikes.filter((k) => k <= f).at(-1), n)];
		case 'straddle':
			return [opt('call', 'buy', atm, n), opt('put', 'buy', atm, n)];
		case 'strangle':
			return [opt('call', 'buy', above[0], n), opt('put', 'buy', below[0], n)];
		case 'iron_condor':
			return [opt('put', 'buy', below[1], n), opt('put', 'sell', below[0], n), opt('call', 'sell', above[0], n), opt('call', 'buy', above[1], n)];
	}
}

const flip = (side: OrderSideName): OrderSideName => (side === 'buy' ? 'sell' : 'buy');

/**
 * A template's legs on one expiry's strikes around the forward `f`, `n` contracts each (the
 * perpetual leg in underlying units). Null when the chain lacks a strike the template needs.
 */
export function templateLegs(id: TemplateId, strikes: readonly number[], f: number, n: number, short = false): BuilderLeg[] | null {
	if (strikes.length === 0) return null;
	const legs = rawLegs(id, strikes, f, n);
	if (legs.some((l) => l == null)) return null;
	const out = legs as BuilderLeg[];
	return short && SHORTABLE.has(id) ? out.map((l) => ({ ...l, side: flip(l.side) })) : out;
}

const rank = (l: BuilderLeg) => (l.kind === 'perp' ? 1 : l.side === 'buy' ? 0 : 2);

/**
 * Submission order (design D3): long options first, then the perpetual, then short options, so the
 * protective longs execute before the shorts they cover (option orders execute in placement order).
 */
export function submissionOrder<T extends BuilderLeg>(legs: readonly T[]): T[] {
	return legs.map((l, i) => ({ l, i })).sort((a, b) => rank(a.l) - rank(b.l) || a.i - b.i).map((x) => x.l);
}

export function writesOptions(legs: readonly BuilderLeg[]): boolean {
	return legs.some((l) => l.kind !== 'perp' && l.side === 'sell');
}

/** Why the legs cannot be combined, or null: at most four option legs and one perpetual leg. */
export function legsProblem(legs: readonly BuilderLeg[]): 'empty' | 'too_many' | 'two_perps' | 'qty' | null {
	if (legs.length === 0) return 'empty';
	if (legs.filter((l) => l.kind !== 'perp').length > MAX_OPTION_LEGS) return 'too_many';
	if (legs.filter((l) => l.kind === 'perp').length > 1) return 'two_perps';
	return legs.every((l) => l.qty > 0) ? null : 'qty';
}

/** Every option leg names a listed series of the one expiry. */
export function sameExpiry(series: readonly (OptionQuoteView | undefined)[]): boolean {
	const listed = series.filter((q): q is OptionQuoteView => q != null);
	return listed.length === series.length && new Set(listed.map((q) => q.expiryStep)).size <= 1;
}

/** Perpetual contracts for `units` of the underlying, on the contract's lot grid. */
export function perpContracts(game: GameState, units: number): string {
	const lot = Number(game.contract.lotSize);
	return qtyToLot(units / Number(game.contract.multiplier), lot);
}

function optionQty(game: GameState, n: number): string {
	return qtyToLot(n, Number(game.options.lotSize));
}

/** One builder leg as the order it sends: a series id (0 = the perpetual), side and quantity text. */
export interface OrderLeg {
	series: number;
	side: OrderSideName;
	qty: string;
}

/** The legs' orders in submission order; null when a strike is not listed on `expiry`. */
export function orderLegs(game: GameState, expiry: number, legs: readonly BuilderLeg[]): OrderLeg[] | null {
	const out: OrderLeg[] = [];
	for (const l of submissionOrder(legs)) {
		if (l.kind === 'perp') {
			out.push({ series: 0, side: l.side, qty: perpContracts(game, l.qty) });
			continue;
		}
		const q = seriesFor(game.options.chain, expiry, l.kind, l.strike ?? NaN);
		if (!q) return null;
		out.push({ series: q.id, side: l.side, qty: optionQty(game, l.qty) });
	}
	return out;
}

export function comboLegs(orders: readonly OrderLeg[]): ComboLegInput[] {
	return orders.map((o) => (o.series === 0 ? { kind: 'perp', side: o.side, qty: o.qty } : { series: o.series, side: o.side, qty: o.qty }));
}

/**
 * The what-if legs of a combo: its orders at the prices they would cross (the engine's default), plus
 * the player's long perpetual when "use my long" replaces the perpetual order (valued from its entry).
 */
export function whatifLegs(orders: readonly OrderLeg[], held: { qty: string; entry: string } | null): WhatIfLegInput[] {
	const legs: WhatIfLegInput[] = comboLegs(orders);
	if (held) legs.push({ kind: 'perp', side: 'buy', qty: held.qty, entry: held.entry });
	return legs;
}

/** Templates whose perpetual leg is the long they protect or write against. */
export const COVERS_LONG: ReadonlySet<TemplateId> = new Set(['covered_call', 'protective_put']);

/**
 * Option contracts that cover a held long perpetual: its underlying units over the option multiplier,
 * down to the option lot. A covered call or protective put on a held long uses this many by default,
 * so one contract never "covers" a long of 48 units.
 */
export function coverContracts(game: GameState, held: { qty: string }): number {
	const units = Number(held.qty) * Number(game.contract.multiplier);
	return Number(qtyToLot(units / Number(game.options.multiplier), Number(game.options.lotSize)));
}

/** The player's long perpetual, when "use my long" can cover a template's perpetual leg. */
export function heldLong(game: GameState): { qty: string; entry: string } | null {
	return Number(game.position.qty) > 0 ? { qty: game.position.qty, entry: game.position.entry } : null;
}

/** The player's open positions as what-if legs at their entries (the terminal lab "from positions"). */
export function positionLegs(game: GameState): WhatIfLegInput[] {
	const abs = (q: string) => String(Math.abs(Number(q)));
	const legs: WhatIfLegInput[] = game.options.positions.map((p) => ({
		series: p.series,
		side: Number(p.qty) > 0 ? 'buy' : 'sell',
		qty: abs(p.qty),
		entry: p.avgPremium
	}));
	const q = Number(game.position.qty);
	if (q !== 0) legs.push({ kind: 'perp', side: q > 0 ? 'buy' : 'sell', qty: abs(game.position.qty), entry: game.position.entry });
	return legs;
}
