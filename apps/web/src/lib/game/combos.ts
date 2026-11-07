// Combos (add-academy-and-strategies design D3): the builder's legs are ordinary logged orders tagged
// with a combo id. This module submits them (cancelling the accepted legs when one is refused),
// rebuilds the combos from the action log, follows their legs through the engine's events (filled,
// cancelled, incomplete) and groups the debrief's trades by combo. Replay ignores the tags.

import type { CommandResult, EngineEvent, EngineTrade, OrderSideName, ReasonCode } from '#lib/engine/types.ts';
import type { OrderLeg, StrategyId } from './strategies.ts';

/** What a combo's logged actions carry (replay ignores both fields). */
export interface ComboTag {
	combo: string;
	strategy: StrategyId;
}

/** The engine calls a submission needs (the session in the game, a fake in tests). */
export interface ComboPort {
	place(leg: OrderLeg, tag: ComboTag): Promise<CommandResult | null>;
	cancel(orderId: number, tag: ComboTag): Promise<CommandResult | null>;
}

export type SubmitOutcome =
	| { ok: true; orderIds: number[] }
	| { ok: false; refusedLeg: number; reason: ReasonCode | 'busy'; cancelled: number[] };

/**
 * Places the legs in the order given (the builder's submission order). When a leg is refused, the legs
 * already accepted are cancelled at once, before the market can advance: nothing of the combo executes.
 */
export async function submitCombo(port: ComboPort, legs: readonly OrderLeg[], tag: ComboTag): Promise<SubmitOutcome> {
	const accepted: number[] = [];
	for (let i = 0; i < legs.length; i++) {
		const r = await port.place(legs[i], tag);
		if (r?.accepted) {
			accepted.push(r.orderId);
			continue;
		}
		for (const id of accepted) await port.cancel(id, tag);
		return { ok: false, refusedLeg: i, reason: r?.reason ?? 'busy', cancelled: accepted };
	}
	return { ok: true, orderIds: accepted };
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
}

export interface ComboRecord {
	id: string;
	strategy: StrategyId;
	/** Decision step the legs were placed at; they execute at the next open. */
	step: number;
	/** In submission order; series 0 = the perpetual. */
	legs: OrderLeg[];
	/** The legs were cancelled at placement (a leg was refused). */
	withdrawn: boolean;
}

/** Combos from an action log: tagged order actions grouped by tag, each at the step it was placed. */
export function combosFromLog(log: readonly LoggedAction[], startStep: number): ComboRecord[] {
	const out = new Map<string, ComboRecord>();
	let step = startStep;
	for (const a of log) {
		if (a.k === 'adv') step += a.n ?? 0;
		if (!a.combo) continue;
		const rec = out.get(a.combo) ?? { id: a.combo, strategy: a.strategy ?? 'custom', step, legs: [], withdrawn: false };
		if (a.k === 'cancel') rec.withdrawn = true;
		else rec.legs.push({ series: a.k === 'opt' ? (a.series ?? 0) : 0, side: a.side ?? 'buy', qty: a.qty ?? '0' });
		out.set(a.combo, rec);
	}
	return [...out.values()];
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
	state: ComboState;
	legs: LegStatus[];
}

const sameQty = (a: string, b: string) => Math.abs(Number(a) - Number(b)) < 1e-9;

/**
 * Order ids of a combo's legs: the run of consecutive orders accepted at the combo's step whose sides
 * and quantities match the legs in order (the legs are placed back to back, with nothing between).
 */
export function comboOrderIds(events: readonly EngineEvent[], rec: ComboRecord): (number | null)[] {
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

function overall(rec: ComboRecord, legs: readonly LegStatus[]): ComboState {
	if (rec.withdrawn) return 'withdrawn';
	if (legs.some((l) => l.state === 'working')) return 'working';
	const filled = legs.filter((l) => l.state === 'filled').length;
	if (filled === legs.length) return 'filled';
	return filled === 0 ? 'not_executed' : 'incomplete';
}

export function comboStatus(events: readonly EngineEvent[], rec: ComboRecord): ComboStatus {
	const ids = comboOrderIds(events, rec);
	const legs = rec.legs.map((leg, i) => legStatus(events, leg, ids[i]));
	return { record: rec, state: overall(rec, legs), legs };
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
		const mine = status.legs.flatMap((l) => {
			const t = l.state === 'filled' ? tradeOfLeg(trades, l) : undefined;
			return t && !used.has(t.n) ? [t] : [];
		});
		mine.forEach((t) => used.add(t.n));
		const net = mine.reduce((s, t) => s + Number(t.net), 0);
		if (mine.length) combos.push({ status, trades: mine, net, r: comboR(mine, net) });
	}
	return { combos, rest: trades.filter((t) => !used.has(t.n)) };
}
