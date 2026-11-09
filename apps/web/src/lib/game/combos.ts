// Combos (add-academy-and-strategies design D3): the builder's legs are ordinary logged orders tagged
// with a combo id. This module submits them (cancelling the accepted legs when one is refused),
// rebuilds the combos from the action log, follows their legs through the engine's events (filled,
// cancelled, incomplete) and groups the debrief's trades by combo. Replay ignores the tags.

import type { CommandResult, EngineEvent, EngineTrade, OrderSideName, ReasonCode, TimeInForceName } from '#lib/engine/types.ts';
import type { OrderLeg, StrategyId } from './strategies.ts';

/** What a combo's logged actions carry (replay ignores every field). */
export interface ComboTag {
	combo: string;
	strategy: StrategyId;
	/** add-practice-and-advanced-strategies D3: the n-th roll or adjustment of the combo ("1", "2", ...). */
	roll?: string;
	adjust?: string;
}

/** The engine calls a submission needs (the session in the game, a fake in tests). */
/** A combo's time in force for its limit legs (design D2 of add-practice-and-advanced-strategies). */
export interface ComboTif {
	tif: TimeInForceName;
	tifBars?: number;
}

export interface ComboPort {
	place(leg: OrderLeg, tag: ComboTag, tif?: ComboTif): Promise<CommandResult | null>;
	cancel(orderId: number, tag: ComboTag): Promise<CommandResult | null>;
}

export type SubmitOutcome =
	| { ok: true; orderIds: number[] }
	| { ok: false; refusedLeg: number; reason: ReasonCode | 'busy'; cancelled: number[] };

/**
 * Places the legs in the order given (the builder's submission order). When a leg is refused, the legs
 * already accepted are cancelled at once, before the market can advance: nothing of the combo executes.
 */
export async function submitCombo(port: ComboPort, legs: readonly OrderLeg[], tag: ComboTag, tif?: ComboTif): Promise<SubmitOutcome> {
	const accepted: number[] = [];
	for (let i = 0; i < legs.length; i++) {
		// Limit legs carry the combo's time in force; market legs ignore it (design D2).
		const r = await port.place(legs[i], tag, legs[i].premium != null ? tif : undefined);
		if (r?.accepted) {
			accepted.push(r.orderId);
			continue;
		}
		for (const id of accepted) await port.cancel(id, tag);
		return { ok: false, refusedLeg: i, reason: r?.reason ?? 'busy', cancelled: accepted };
	}
	return { ok: true, orderIds: accepted };
}

/**
 * The notice after a combo's legs are placed: a limit leg is not "executed at the next open" (playtest:
 * an IOC limit condor said so and then nothing filled).
 */
export function comboSentKey(orders: readonly OrderLeg[]): 'combo.sent' | 'combo.sentLimit' {
	return orders.some((o) => o.premium != null) ? 'combo.sentLimit' : 'combo.sent';
}

/** The next free combo id ("c1", "c2", ...). */
export function nextComboId(combos: readonly { id: string }[]): string {
	const used = combos.map((c) => Number(c.id.slice(1))).filter((n) => Number.isFinite(n));
	return `c${Math.max(0, ...used) + 1}`;
}

/** A logged action, as far as combos need it (the session's Action is a subtype). */
export interface LoggedAction {
	k: string;
	n?: number;
	series?: number;
	side?: OrderSideName;
	qty?: string;
	combo?: string;
	strategy?: StrategyId;
	roll?: string;
	adjust?: string;
	premium?: string | null;
	type?: string;
}

/** One submission of a combo: its opening, a roll or an adjustment, placed at one decision step. */
export interface ComboSubmission {
	kind: 'open' | 'roll' | 'adjust';
	/** The roll's or adjustment's number ("1", ...); "" for the opening. */
	n: string;
	step: number;
	legs: OrderLeg[];
	/** The legs were cancelled at placement (a leg was refused). */
	withdrawn: boolean;
}

export interface ComboRecord {
	id: string;
	strategy: StrategyId;
	/** Decision step the opening legs were placed at; they execute at the next open. */
	step: number;
	/** The opening legs in submission order; series 0 = the perpetual. */
	legs: OrderLeg[];
	/** The opening legs were cancelled at placement (a leg was refused). */
	withdrawn: boolean;
	/** The opening, then every roll and adjustment, in log order (one journal and debrief row). */
	submissions: ComboSubmission[];
}

function submissionKey(a: LoggedAction): { kind: ComboSubmission['kind']; n: string } {
	if (a.roll) return { kind: 'roll', n: a.roll };
	if (a.adjust) return { kind: 'adjust', n: a.adjust };
	return { kind: 'open', n: '' };
}

function legOf(a: LoggedAction): OrderLeg {
	const leg: OrderLeg = { series: a.k === 'opt' ? (a.series ?? 0) : 0, side: a.side ?? 'buy', qty: a.qty ?? '0' };
	return a.k === 'opt' && a.type === 'limit' && a.premium ? { ...leg, premium: a.premium } : leg;
}

/** Combos from an action log: tagged order actions grouped by tag, each submission at the step it was placed. */
export function combosFromLog(log: readonly LoggedAction[], startStep: number): ComboRecord[] {
	const out = new Map<string, ComboRecord>();
	let step = startStep;
	for (const a of log) {
		if (a.k === 'adv') step += a.n ?? 0;
		if (!a.combo) continue;
		const rec = out.get(a.combo) ?? { id: a.combo, strategy: a.strategy ?? 'custom', step, legs: [], withdrawn: false, submissions: [] };
		const key = submissionKey(a);
		let sub = rec.submissions.find((x) => x.kind === key.kind && x.n === key.n);
		if (!sub) {
			sub = { ...key, step, legs: [], withdrawn: false };
			rec.submissions.push(sub);
		}
		if (a.k === 'cancel') sub.withdrawn = true;
		else sub.legs.push(legOf(a));
		if (key.kind === 'open') {
			rec.legs = sub.legs;
			rec.withdrawn = sub.withdrawn;
		}
		out.set(a.combo, rec);
	}
	return [...out.values()];
}

/** The next roll or adjustment number of a combo ("1", "2", ...). */
export function nextSubmissionNumber(rec: Pick<ComboRecord, 'submissions'>, kind: 'roll' | 'adjust'): string {
	return String(rec.submissions.filter((x) => x.kind === kind).length + 1);
}

export type LegState = 'working' | 'filled' | 'cancelled';

export interface LegStatus {
	leg: OrderLeg;
	orderId: number | null;
	state: LegState;
	reason: ReasonCode | null;
	/** Fill price (premium per unit for options) and the bar it executed at. */
	price: string | null;
	fillStep: number | null;
}

export type ComboState = 'working' | 'filled' | 'withdrawn' | 'not_executed' | 'incomplete';

export interface ComboStatus {
	record: ComboRecord;
	/** The latest submission's state (the opening's until a roll or an adjustment). */
	state: ComboState;
	/** The latest submission's legs. */
	legs: LegStatus[];
	/** Every submission's legs, in order: the combo's whole history. */
	allLegs: LegStatus[];
}

const sameQty = (a: string, b: string) => Math.abs(Number(a) - Number(b)) < 1e-9;

/**
 * Order ids of a combo's legs: the run of consecutive orders accepted at the combo's step whose sides
 * and quantities match the legs in order (the legs are placed back to back, with nothing between).
 */
export function comboOrderIds(events: readonly EngineEvent[], rec: Pick<ComboSubmission, 'step' | 'legs'>): (number | null)[] {
	const accepted = events.filter((e) => e.type === 'order' && e.status === 'accepted' && e.step === rec.step);
	const n = rec.legs.length;
	for (let start = accepted.length - n; start >= 0; start--) {
		const run = accepted.slice(start, start + n);
		if (run.every((e, i) => e.side === rec.legs[i].side && sameQty(e.qty, rec.legs[i].qty))) return run.map((e) => e.orderId);
	}
	return rec.legs.map(() => null);
}

function legStatus(events: readonly EngineEvent[], leg: OrderLeg, orderId: number | null): LegStatus {
	const base: LegStatus = { leg, orderId, state: 'working', reason: null, price: null, fillStep: null };
	if (orderId == null) return base;
	const fill = events.find((e) => e.type === 'fill' && e.orderId === orderId);
	if (fill) return { ...base, state: 'filled', price: fill.price, fillStep: fill.step };
	const end = events.find((e) => e.type === 'order' && e.orderId === orderId && (e.status === 'cancelled' || e.status === 'expired'));
	return end ? { ...base, state: 'cancelled', reason: end.reason } : base;
}

function overall(rec: Pick<ComboSubmission, 'withdrawn'>, legs: readonly LegStatus[]): ComboState {
	if (rec.withdrawn) return 'withdrawn';
	if (legs.some((l) => l.state === 'working')) return 'working';
	const filled = legs.filter((l) => l.state === 'filled').length;
	if (filled === legs.length) return 'filled';
	return filled === 0 ? 'not_executed' : 'incomplete';
}

function submissionLegs(events: readonly EngineEvent[], sub: Pick<ComboSubmission, 'step' | 'legs'>): LegStatus[] {
	const ids = comboOrderIds(events, sub);
	return sub.legs.map((leg, i) => legStatus(events, leg, ids[i]));
}

export function comboStatus(events: readonly EngineEvent[], rec: ComboRecord): ComboStatus {
	const subs = rec.submissions?.length ? rec.submissions : [{ kind: 'open' as const, n: '', step: rec.step, legs: rec.legs, withdrawn: rec.withdrawn }];
	const per = subs.map((sub) => ({ sub, legs: submissionLegs(events, sub) }));
	const last = per[per.length - 1];
	return { record: rec, state: overall(last.sub, last.legs), legs: last.legs, allLegs: per.flatMap((x) => x.legs) };
}

/**
 * The combo's live holdings (design D3: what a roll or an adjustment starts from): the filled legs of
 * every submission netted per series (signed contracts or perpetual contracts, > 0 long).
 */
export function comboHoldings(status: Pick<ComboStatus, 'allLegs'>): { series: number; qty: number }[] {
	const net = new Map<number, number>();
	for (const l of status.allLegs) {
		if (l.state !== 'filled') continue;
		const q = Number(l.leg.qty) * (l.leg.side === 'buy' ? 1 : -1);
		net.set(l.leg.series, (net.get(l.leg.series) ?? 0) + q);
	}
	return [...net.entries()].filter(([, q]) => Math.abs(q) > 1e-9).map(([series, qty]) => ({ series, qty: Math.round(qty * 1e8) / 1e8 }));
}

/** The order ids of a combo's legs still working (design D2's "Cancel working legs"). */
export function workingLegs(status: Pick<ComboStatus, 'legs'>): number[] {
	return status.legs.filter((l) => l.state === 'working' && l.orderId != null).map((l) => l.orderId!);
}

/** Option premium cash of the filled legs, per contract unit times the multiplier: credit > 0, debit < 0. */
export function netPremium(status: ComboStatus, multiplier: number): number {
	let total = 0;
	for (const l of status.legs) {
		if (l.state !== 'filled' || l.leg.series === 0 || l.price == null) continue;
		const cash = Number(l.price) * Number(l.leg.qty) * multiplier;
		total += l.leg.side === 'sell' ? cash : -cash;
	}
	return total;
}

/** The legs a player still carries after an incomplete combo: filled ones. */
export function openLegs(status: ComboStatus): LegStatus[] {
	return status.state === 'incomplete' ? status.legs.filter((l) => l.state === 'filled') : [];
}

export interface ComboTrades {
	status: ComboStatus;
	trades: EngineTrade[];
	/** Sum of the trades' net results (fees and funding included). */
	net: number;
	/** The combo's net over its planned risk; null without a plan. Its legs' own R mean little. */
	r: number | null;
}

/** The contracts a combo's plan headline names: its first option leg's (all legs share it), else the first leg's. */
export function comboContracts(orders: readonly OrderLeg[]): string {
	return (orders.find((o) => o.series !== 0) ?? orders[0])?.qty ?? '0';
}

/**
 * The combo's R: its plan's risk is split evenly over the legs (one engine plan per leg), so each leg's
 * planned risk is its net over its R, and the combo's risk is that share times the legs.
 */
export function comboR(trades: readonly EngineTrade[], net: number): number | null {
	const leg = trades.find((t) => t.rBp != null && t.rBp !== 0 && Number(t.net) !== 0);
	if (!leg) return null;
	const share = Number(leg.net) / (leg.rBp! / 10000);
	return share > 0 ? net / (share * trades.length) : null;
}

function tradeOfLeg(trades: readonly EngineTrade[], l: LegStatus): EngineTrade | undefined {
	const side = l.leg.side === 'buy' ? 'long' : 'short';
	return trades.find((t) => t.series === l.leg.series && t.entryStep === l.fillStep && t.side === side);
}

/**
 * The debrief's trades grouped by combo (design D3 "Combos in the journal and debrief"): each combo with
 * the trades its filled legs opened, and the trades that belong to no combo.
 */
export function groupTrades(trades: readonly EngineTrade[], statuses: readonly ComboStatus[]): { combos: ComboTrades[]; rest: EngineTrade[] } {
	const used = new Set<number>();
	const combos: ComboTrades[] = [];
	for (const status of statuses) {
		// A rolled or adjusted combo stays one row: every submission's legs (design D3).
		const mine = (status.allLegs ?? status.legs).flatMap((l) => {
			const t = l.state === 'filled' ? tradeOfLeg(trades, l) : undefined;
			return t && !used.has(t.n) ? [t] : [];
		});
		mine.forEach((t) => used.add(t.n));
		const net = mine.reduce((s, t) => s + Number(t.net), 0);
		if (mine.length) combos.push({ status, trades: mine, net, r: comboR(mine, net) });
	}
	return { combos, rest: trades.filter((t) => !used.has(t.n)) };
}
