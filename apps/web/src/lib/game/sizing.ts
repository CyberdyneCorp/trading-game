// Pre-trade estimates shown on the order ticket (spec terminal-ui "Order ticket for perpetuals").
// Estimates only: the engine is authoritative for acceptance, fills, margin and liquidation.

import type { Contract } from '#lib/engine/types.ts';

export type Side = 'buy' | 'sell';

export interface TicketInput {
	side: Side;
	qty: number;
	entry: number; // estimated entry price
	leverage: number;
	stop: number | null;
}

export interface TicketEstimate {
	notional: number;
	margin: number;
	fee: number;
	liquidation: number | null;
	plannedRisk: number | null; // loss to stop including round-trip taker fees
	stopBeyondLiquidation: boolean;
}

/** A maintenance-margin tier: from `floor` notional (at entry) up, rate `mmr` and at most `maxLeverage`. */
export interface Tier {
	floor: number;
	mmr: number;
	maxLeverage: number;
}

export interface Costs {
	multiplier: number;
	takerFee: number;
	/** Base maintenance rate; the tiers (policy v16), when present, start with it. */
	maintenanceMargin: number;
	halfSpread: number;
	slippage: number;
	tiers?: Tier[];
}

export function costsOf(c: Contract): Costs {
	return {
		multiplier: Number(c.multiplier),
		takerFee: Number(c.takerFee),
		maintenanceMargin: Number(c.maintenanceMargin),
		halfSpread: Number(c.halfSpread),
		slippage: Number(c.slippage),
		tiers: (c.marginTiers ?? []).map((t) => ({ floor: Number(t.notionalFloor), mmr: Number(t.maintenanceMargin), maxLeverage: t.maxLeverage }))
	};
}

/** The tier of a position of `notional` at its entry, as the engine picks it (the last floor reached). */
export function marginTier(k: Costs, notional: number): Tier {
	let tier: Tier = { floor: 0, mmr: k.maintenanceMargin, maxLeverage: Infinity };
	for (const t of k.tiers ?? []) {
		if (notional < t.floor) break;
		tier = t;
	}
	return tier;
}

/** Market orders fill at the next open adjusted against the player; estimate from the last close. */
export function estimatedMarketEntry(side: Side, mark: number, k: Costs): number {
	const adj = k.halfSpread + k.slippage;
	return side === 'buy' ? mark * (1 + adj) : mark * (1 - adj);
}

/**
 * Isolated-margin liquidation price (same formula as the engine, execution-policy v1), at the
 * maintenance rate of the tier of the position's notional at entry (policy v16).
 */
export function liquidationPrice(side: Side, qty: number, entry: number, margin: number, k: Costs): number | null {
	const u = qty * k.multiplier;
	if (u <= 0) return null;
	const mmr = marginTier(k, u * entry).mmr;
	if (side === 'buy') {
		const num = u * entry - margin;
		return num <= 0 ? null : num / (u * (1 - mmr));
	}
	return (u * entry + margin) / (u * (1 + mmr));
}

/**
 * Liquidation of an open position after its leverage is changed: the engine re-margins the whole
 * position at the new leverage (margin = notional at entry / leverage), not only the new order.
 */
export function releveredLiquidation(side: Side, qty: number, entry: number, leverage: number, k: Costs): number | null {
	const margin = (Math.abs(qty) * entry * k.multiplier) / Math.max(1, leverage);
	return liquidationPrice(side, Math.abs(qty), entry, margin, k);
}

/** The open position the ticket's order lands on: signed qty, entry, isolated margin and its leverage. */
export interface OpenPosition {
	qty: number;
	entry: number;
	margin: number;
	leverage: number;
}

/**
 * Liquidation of the position the order leads to, as the engine books it: an add averages the entry
 * and adds the order's initial margin, a reduction releases margin pro rata, a reversal opens the
 * remainder on its own; the tier is that of the resulting notional (policy v16). A different
 * leverage re-margins the open part first (engine set_leverage).
 */
export function liquidationAfter(input: TicketInput, pos: OpenPosition, k: Costs): number | null {
	const { side, qty, entry, leverage } = input;
	const held = Math.abs(pos.qty);
	const lev = Math.max(1, leverage);
	const heldMargin = lev === pos.leverage ? pos.margin : (held * pos.entry * k.multiplier) / lev;
	const posSide: Side = pos.qty > 0 ? 'buy' : 'sell';
	if (held === 0 || posSide === side) {
		const total = held + qty;
		const avg = total > 0 ? (held * pos.entry + qty * entry) / total : entry;
		return liquidationPrice(side, total, avg, heldMargin + (qty * entry * k.multiplier) / lev, k);
	}
	if (qty < held) return liquidationPrice(posSide, held - qty, pos.entry, (heldMargin * (held - qty)) / held, k);
	const rest = qty - held;
	return rest > 0 ? liquidationPrice(side, rest, entry, (rest * entry * k.multiplier) / lev, k) : null;
}

export function estimate(input: TicketInput, k: Costs, pos?: OpenPosition): TicketEstimate {
	const { side, qty, entry, leverage, stop } = input;
	const notional = qty * k.multiplier * entry;
	const margin = notional / Math.max(1, leverage);
	const fee = notional * k.takerFee;
	const liquidation = pos ? liquidationAfter(input, pos, k) : liquidationPrice(side, qty, entry, margin, k);
	// A reduction leaves the position on the other side: its liquidation says nothing about this stop.
	const onOrderSide = !pos || pos.qty === 0 || pos.qty > 0 === (side === 'buy') || qty > Math.abs(pos.qty);
	let plannedRisk: number | null = null;
	let stopBeyondLiquidation = false;
	if (stop != null && stop > 0) {
		const exitAdj = side === 'buy' ? stop * (1 - k.halfSpread - k.slippage) : stop * (1 + k.halfSpread + k.slippage);
		const perUnit = side === 'buy' ? entry - exitAdj : exitAdj - entry;
		plannedRisk = perUnit * qty * k.multiplier + fee + qty * k.multiplier * exitAdj * k.takerFee;
		if (liquidation != null && onOrderSide) stopBeyondLiquidation = side === 'buy' ? stop <= liquidation : stop >= liquidation;
	}
	return { notional, margin, fee, liquidation, plannedRisk, stopBeyondLiquidation };
}

/**
 * Quantity whose loss at the stop (incl. estimated costs) equals riskPct of equity,
 * rounded down to the lot (spec: "Sizing" — e.g. 10,000 x 1% / (5 + 1) = 16.6).
 */
export function qtyForRisk(equity: number, riskPct: number, entry: number, stop: number, k: Costs, lot: number): number {
	const budget = equity * (riskPct / 100);
	const distance = Math.abs(entry - stop);
	if (budget <= 0 || distance <= 0) return 0;
	const costPerUnit = entry * k.takerFee + stop * (k.takerFee + k.halfSpread + k.slippage);
	const units = budget / ((distance + costPerUnit) * k.multiplier);
	return Math.floor(units / lot + 1e-9) * lot;
}

/** What the engine reserves to accept an order: initial margin plus the taker fee on the part that adds exposure. */
export function requiredMargin(incQty: number, entry: number, leverage: number, k: Costs): number {
	const notional = incQty * k.multiplier * entry;
	return notional / Math.max(1, leverage) + notional * k.takerFee;
}

/**
 * Fits an order to the available balance, so a suggested size never fills a ticket the engine would
 * reject: the lowest whole leverage from `from` up to `cap` that covers its margin, else the largest
 * quantity (to the lot) that fits at the cap.
 */
export function fitToMargin(
	qty: number,
	entry: number,
	available: number,
	from: number,
	cap: number,
	k: Costs,
	lot: number
): { qty: number; leverage: number } {
	// A large size sits in a margin tier that may allow less leverage than the mission (policy v16).
	cap = Math.min(cap, marginTier(k, qty * k.multiplier * entry).maxLeverage);
	for (let lev = Math.max(1, from); lev <= cap; lev++) {
		if (requiredMargin(qty, entry, lev, k) <= available) return { qty, leverage: lev };
	}
	const perUnit = requiredMargin(1, entry, cap, k);
	const fits = perUnit > 0 ? Math.floor(available / perUnit / lot + 1e-9) * lot : 0;
	return { qty: Math.min(qty, fits), leverage: cap };
}

export interface OpposingEffect {
	kind: 'reduce' | 'close' | 'reverse';
	/** Size of the open position (absolute). */
	open: number;
	/** What is left after the fill: the reduced position, or the new position on the other side. */
	after: number;
}

/**
 * What an order on the opposite side of the open position does to it (null for a flat account or the
 * same side): a risk-sized "new" trade can mostly just close the old one, so the ticket says so.
 */
export function opposingEffect(position: number, side: Side, qty: number, lot: number): OpposingEffect | null {
	if (position === 0 || qty <= 0 || position > 0 === (side === 'buy')) return null;
	const open = Math.abs(position);
	const diff = Math.round((qty - open) / lot) * lot;
	if (diff < 0) return { kind: 'reduce', open, after: -diff };
	return { kind: diff === 0 ? 'close' : 'reverse', open, after: diff };
}
