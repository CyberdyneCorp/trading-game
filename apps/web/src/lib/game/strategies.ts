// Strategy builder model (add-academy-and-strategies design D3): templates as legs on the listed chain,
// the submission order and the requests for the engine's what-if valuation and combo dry run. Every
// number the builder shows comes from those engine calls; this module only chooses legs.

import type { ComboLegInput, GameState, OptionQuoteView, OrderSideName, TimeInForceName, WhatIfLegInput } from '#lib/engine/types.ts';
import { qtyToLot } from './format.ts';

export type TemplateId =
	| 'bull_call'
	| 'bear_put'
	| 'covered_call'
	| 'protective_put'
	| 'straddle'
	| 'strangle'
	| 'iron_condor'
	// Two expiries (add-practice-and-advanced-strategies D3): need a later listed expiry.
	| 'calendar_call'
	| 'calendar_put'
	| 'diagonal_call'
	| 'diagonal_put';

/** Templates on two expiries: the near one sold, the far one bought (the long version). */
export const TWO_EXPIRY: ReadonlySet<TemplateId> = new Set(['calendar_call', 'calendar_put', 'diagonal_call', 'diagonal_put']);

/** A combo's structure name: a template, its short version, or free legs. */
export type StrategyId =
	| TemplateId
	| 'short_straddle'
	| 'short_strangle'
	| 'short_calendar_call'
	| 'short_calendar_put'
	| 'short_diagonal_call'
	| 'short_diagonal_put'
	| 'custom';

export const TEMPLATES: readonly TemplateId[] = [
	'bull_call',
	'bear_put',
	'covered_call',
	'protective_put',
	'straddle',
	'strangle',
	'iron_condor',
	'calendar_call',
	'calendar_put',
	'diagonal_call',
	'diagonal_put'
];

/**
 * The templates of one expiry: the run-less Academy builder lists only these, since a lesson market
 * has a single expiry (playtest: its calendars and diagonals built no legs). Desktop kOneExpiryTemplates.
 */
export const ONE_EXPIRY_TEMPLATES: readonly TemplateId[] = TEMPLATES.filter((id) => !TWO_EXPIRY.has(id));

/** Templates whose legs write options (sell to open): disabled when the run does not allow writing. */
const WRITES: ReadonlySet<TemplateId> = new Set(['bull_call', 'bear_put', 'covered_call', 'iron_condor', ...TWO_EXPIRY]);
/** Templates offered short (every side flipped). */
const SHORTABLE: ReadonlySet<TemplateId> = new Set(['straddle', 'strangle', ...TWO_EXPIRY]);

export const MAX_OPTION_LEGS = 4;

export type LegKind = 'call' | 'put' | 'perp';

export interface BuilderLeg {
	kind: LegKind;
	side: OrderSideName;
	/** Option strike; null for the perpetual. */
	strike: number | null;
	/** Contracts (options) or underlying units (the perpetual: contracts x the option multiplier). */
	qty: number;
	/** The leg's expiry step (design D3: up to two per structure); absent = the builder's expiry. */
	expiry?: number;
	/** Design D2: a limit premium (option legs); absent = a market leg. */
	limit?: number;
}

export function templateWrites(id: TemplateId, short: boolean): boolean {
	return WRITES.has(id) || (short && SHORTABLE.has(id));
}

export function templateShortable(id: TemplateId): boolean {
	return SHORTABLE.has(id);
}

export function strategyOf(id: TemplateId, short: boolean): StrategyId {
	return short && SHORTABLE.has(id) ? (`short_${id}` as StrategyId) : id;
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

/**
 * Calendar and diagonal legs (design D3): the near expiry sold, the far one bought. A calendar uses the
 * strike at the money on both; a diagonal sells the next strike out of the money (above for calls,
 * below for puts) and buys the strike at the money, so K1 < K2 for calls and K1 > K2 for puts.
 */
function twoExpiryLegs(id: TemplateId, near: readonly number[], far: readonly number[], f: number, n: number, e: { near: number; far: number }): (BuilderLeg | null)[] {
	const kind = id.endsWith('call') ? 'call' : 'put';
	const atm = nearest(near, f);
	const i = near.indexOf(atm);
	const farAtm = far.includes(atm) ? atm : undefined;
	const shortStrike = id.startsWith('calendar') ? atm : near[kind === 'call' ? i + 1 : i - 1];
	const withExpiry = (l: BuilderLeg | null, expiry: number) => (l ? { ...l, expiry } : null);
	return [withExpiry(opt(kind, 'sell', shortStrike, n), e.near), withExpiry(opt(kind, 'buy', farAtm, n), e.far)];
}

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
		default:
			return [null]; // two-expiry templates: twoExpiryTemplateLegs
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

/**
 * A calendar or diagonal on two expiries (design D3), `n` contracts each; null when the far expiry lacks
 * the strike at the money or the near one lacks the strike the template sells.
 */
export function twoExpiryTemplateLegs(
	id: TemplateId,
	near: { step: number; strikes: readonly number[] },
	far: { step: number; strikes: readonly number[] },
	f: number,
	n: number,
	short = false
): BuilderLeg[] | null {
	if (!TWO_EXPIRY.has(id) || near.strikes.length === 0 || far.step <= near.step) return null;
	const legs = twoExpiryLegs(id, near.strikes, far.strikes, f, n, { near: near.step, far: far.step });
	if (legs.some((l) => l == null)) return null;
	const out = legs as BuilderLeg[];
	return short ? out.map((l) => ({ ...l, side: flip(l.side) })) : out;
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

/** Why the legs cannot be combined, or null: at most four option legs, one perpetual leg, two expiries. */
export function legsProblem(legs: readonly BuilderLeg[], expiry = 0): 'empty' | 'too_many' | 'two_perps' | 'qty' | 'expiries' | null {
	if (legs.length === 0) return 'empty';
	if (legs.filter((l) => l.kind !== 'perp').length > MAX_OPTION_LEGS) return 'too_many';
	if (legs.filter((l) => l.kind === 'perp').length > 1) return 'two_perps';
	if (new Set(legs.filter((l) => l.kind !== 'perp').map((l) => l.expiry ?? expiry)).size > 2) return 'expiries';
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
	/** Design D2: an option limit leg's premium on the premium tick; absent = market. */
	premium?: string;
}

/** A premium on the tick grid as text (the engine's premium tick). */
export function premiumText(v: number, tick: number): string {
	const decimals = Math.max(0, Math.round(-Math.log10(tick)));
	return (Math.round(v / tick) * tick).toFixed(decimals);
}

/**
 * The default limit of a leg (design D2): the quote's mid rounded to the tick toward the passive side,
 * down for a buy and up for a sell, never below one tick.
 */
export function defaultLimit(q: Pick<OptionQuoteView, 'mid'>, side: OrderSideName, tick: number): number {
	const ticks = Number(q.mid) / tick;
	const n = side === 'buy' ? Math.floor(ticks + 1e-9) : Math.ceil(ticks - 1e-9);
	return Math.max(1, n) * tick;
}

/** The legs' orders in submission order; null when a strike is not listed on its leg's expiry. */
export function orderLegs(game: GameState, expiry: number, legs: readonly BuilderLeg[]): OrderLeg[] | null {
	const out: OrderLeg[] = [];
	const tick = Number(game.options.premiumTick);
	for (const l of submissionOrder(legs)) {
		if (l.kind === 'perp') {
			out.push({ series: 0, side: l.side, qty: perpContracts(game, l.qty) });
			continue;
		}
		const q = seriesFor(game.options.chain, l.expiry ?? expiry, l.kind, l.strike ?? NaN);
		if (!q) return null;
		out.push({ series: q.id, side: l.side, qty: optionQty(game, l.qty), ...(l.limit != null ? { premium: premiumText(l.limit, tick) } : {}) });
	}
	return out;
}

/** The dry run's legs: limit legs carry their premium and the combo's time in force (D2). */
export function comboLegs(orders: readonly OrderLeg[], tif?: { tif: TimeInForceName; tifBars?: number }): ComboLegInput[] {
	return orders.map((o) => {
		if (o.series === 0) return { kind: 'perp', side: o.side, qty: o.qty };
		const limit = o.premium != null ? { type: 'limit' as const, premium: o.premium, ...(tif && tif.tif !== 'gtc' ? tif : {}) } : {};
		return { series: o.series, side: o.side, qty: o.qty, ...limit };
	});
}

/** The expiries the option legs use (one or two), ascending. */
export function legExpiries(legs: readonly BuilderLeg[], expiry: number): number[] {
	return [...new Set(legs.filter((l) => l.kind !== 'perp').map((l) => l.expiry ?? expiry))].sort((a, b) => a - b);
}

/** A held long perpetual: its units, entry and the working stop that protects all of it, if any. */
export interface HeldLong {
	qty: string;
	entry: string;
	/** Trigger of the reduce-only stop covering the whole long (the highest when several do). */
	stop?: string;
}

/**
 * The what-if legs of a combo: its orders at the prices they would cross (the engine's default), plus
 * the player's long perpetual when "use my long" replaces the perpetual order (valued from its entry).
 * The long carries its stop, so the engine's maximum loss (the combo's planned risk) is the loss to
 * that stop, not the long's whole value down to a price of 0: the long's own risk was already planned.
 */
export function whatifLegs(orders: readonly OrderLeg[], held: HeldLong | null): WhatIfLegInput[] {
	// A limit leg is valued at its limit premium (design D2); a market leg at the price it would cross.
	const legs: WhatIfLegInput[] = orders.map((o) =>
		o.series === 0 ? { kind: 'perp', side: o.side, qty: o.qty } : { series: o.series, side: o.side, qty: o.qty, ...(o.premium != null ? { entry: o.premium } : {}) }
	);
	if (held) legs.push({ kind: 'perp', side: 'buy', qty: held.qty, entry: held.entry, ...(held.stop ? { stop: held.stop } : {}) });
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

const STOP_TYPES: ReadonlySet<string> = new Set(['stop_market', 'trailing_stop']);

/** The highest trigger of the working reduce-only sell stops that close the whole long (null: none). */
export function longStop(game: GameState): string | null {
	const units = Number(game.position.qty);
	const covering = (game.orders ?? []).filter(
		(o) => o.series === 0 && o.reduceOnly && o.side === 'sell' && STOP_TYPES.has(o.type) && Number(o.price) > 0 && Number(o.qty) - Number(o.filled) >= units
	);
	if (units <= 0 || covering.length === 0) return null;
	return covering.reduce((best, o) => (Number(o.price) > Number(best.price) ? o : best)).price;
}

/** The player's long perpetual, when "use my long" can cover a template's perpetual leg. */
export function heldLong(game: GameState): HeldLong | null {
	if (Number(game.position.qty) <= 0) return null;
	const stop = longStop(game);
	return { qty: game.position.qty, entry: game.position.entry, ...(stop ? { stop } : {}) };
}

/**
 * Contracts per leg so that the structure's maximum loss is `riskPct` % of equity (the builder's
 * sizing aid, like the perpetual ticket's risk sizing): the engine's maximum loss at `n` contracts
 * scaled to the budget, down to the option lot. Null without a bounded, positive maximum loss.
 */
export function contractsForRisk(equity: number, riskPct: number, maxLoss: number | null, n: number, lot: number): number | null {
	if (maxLoss == null || !(maxLoss > 0) || !(n > 0) || !(riskPct > 0) || !(equity > 0) || !(lot > 0)) return null;
	const perContract = maxLoss / n;
	return Number(qtyToLot((equity * riskPct) / 100 / perContract, lot));
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

// ---- roll (design D3) ----------------------------------------------------------------------------

/** A held option leg of a combo: series and signed contracts (> 0 long). */
export interface HeldLeg {
	series: number;
	qty: number;
}

export interface RollPlan {
	/** Closing every held leg of the near expiry, then opening the same structure on `to`. */
	orders: OrderLeg[];
	from: number;
	to: number;
}

function strikeNearest(strikes: readonly number[], k: number): number | undefined {
	return strikes.length ? strikes.reduce((best, x) => (Math.abs(x - k) < Math.abs(best - k) ? x : best), strikes[0]) : undefined;
}

/**
 * The orders of a roll (design D3): close every held option leg of the near expiry and open the same
 * structure on the later expiry `to`, at the same strikes when listed (else the nearest listed), or
 * re-centred at the money (every strike shifted by the ATM strike minus the structure's middle strike).
 * Ordered buys, then sells, so protection is added before it is removed. Null when `to` is not a
 * roll target (`rollTargets`: later than the near expiry and not held by the combo) or a strike
 * cannot be found.
 */
export function rollOrders(game: GameState, held: readonly HeldLeg[], to: number, recenter: boolean, f: number): RollPlan | null {
	const chain = game.options.chain;
	const legs = held.flatMap((h) => {
		const q = chain.find((x) => x.id === h.series);
		return q && h.qty !== 0 ? [{ h, q }] : [];
	});
	if (legs.length === 0) return null;
	const from = Math.min(...legs.map((l) => l.q.expiryStep));
	if (!rollTargets(chain, from, legs.map((l) => l.q.expiryStep)).includes(to)) return null;
	const near = legs.filter((l) => l.q.expiryStep === from);
	const strikes = strikesOf(chain, to);
	const ks = near.map((l) => Number(l.q.strike));
	const shift = recenter ? (strikeNearest(strikes, f) ?? f) - (Math.min(...ks) + Math.max(...ks)) / 2 : 0;
	const out: BuilderLeg[] = [];
	for (const { h, q } of near) {
		const k = strikeNearest(strikes, Number(q.strike) + shift);
		const target = k != null ? seriesFor(chain, to, q.kind, k) : undefined;
		if (!target) return null;
		const abs = Math.abs(h.qty);
		out.push({ kind: q.kind, side: h.qty > 0 ? 'sell' : 'buy', strike: Number(q.strike), qty: abs, expiry: from });
		out.push({ kind: q.kind, side: h.qty > 0 ? 'buy' : 'sell', strike: k!, qty: abs, expiry: to });
	}
	const orders = submissionOrder(out).map((l) => {
		const q = seriesFor(chain, l.expiry!, l.kind as 'call' | 'put', l.strike!)!;
		return { series: q.id, side: l.side, qty: optionQty(game, l.qty) };
	});
	return { orders, from, to };
}

/**
 * The builder's margin label: the spread rule pairs a covered sale only within one expiry unless the
 * mission declares the calendar margin (design D5), so a two-expiry structure on a mission without it
 * pays the sale's own margin (playtest: m17's calendar was labelled "a covered sale pays only the
 * pair's maximum loss" over a naked margin).
 */
export function marginLabelKey(
	rules: { spreadMargin?: boolean; calendarMargin?: boolean },
	twoExpiries: boolean
): 'builder.marginPerLeg' | 'builder.marginSameExpiry' | 'builder.marginSpread' {
	if (!rules.spreadMargin) return 'builder.marginPerLeg';
	return twoExpiries && !rules.calendarMargin ? 'builder.marginSameExpiry' : 'builder.marginSpread';
}

/**
 * Later listed expiries a roll from `from` can go to (the next one first), leaving out the expiries the
 * combo already holds (`held`): rolling a calendar's short onto its long's expiry would net the two and
 * close the calendar instead of rolling it.
 */
export function rollTargets(chain: readonly OptionQuoteView[], from: number, held: readonly number[] = []): number[] {
	return expiriesOf(chain).filter((e) => e > from && !held.includes(e));
}

/**
 * The perpetual hedge to neutral (design D9): -deltaUnits in contracts, rounded toward zero to the lot.
 * Null when that is less than one lot.
 */
export function hedgeOrder(game: GameState, deltaUnits: number): OrderLeg | null {
	const lot = Number(game.contract.lotSize);
	const contracts = -deltaUnits / Number(game.contract.multiplier);
	const lots = Math.trunc(Math.abs(contracts) / lot + 1e-9);
	if (lots < 1) return null;
	return { series: 0, side: contracts > 0 ? 'buy' : 'sell', qty: qtyToLot(lots * lot, lot) };
}
