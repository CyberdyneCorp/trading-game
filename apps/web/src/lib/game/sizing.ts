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

export interface Costs {
	multiplier: number;
	takerFee: number;
	maintenanceMargin: number;
	halfSpread: number;
	slippage: number;
}

export function costsOf(c: Contract): Costs {
	return {
		multiplier: Number(c.multiplier),
		takerFee: Number(c.takerFee),
		maintenanceMargin: Number(c.maintenanceMargin),
		halfSpread: Number(c.halfSpread),
		slippage: Number(c.slippage)
	};
}

/** Market orders fill at the next open adjusted against the player; estimate from the last close. */
export function estimatedMarketEntry(side: Side, mark: number, k: Costs): number {
	const adj = k.halfSpread + k.slippage;
	return side === 'buy' ? mark * (1 + adj) : mark * (1 - adj);
}

/** Isolated-margin liquidation price (same formula as the engine, execution-policy v1). */
export function liquidationPrice(side: Side, qty: number, entry: number, margin: number, k: Costs): number | null {
	const u = qty * k.multiplier;
	if (u <= 0) return null;
	if (side === 'buy') {
		const num = u * entry - margin;
		return num <= 0 ? null : num / (u * (1 - k.maintenanceMargin));
	}
	return (u * entry + margin) / (u * (1 + k.maintenanceMargin));
}

/**
 * Liquidation of an open position after its leverage is changed: the engine re-margins the whole
 * position at the new leverage (margin = notional at entry / leverage), not only the new order.
 */
export function releveredLiquidation(side: Side, qty: number, entry: number, leverage: number, k: Costs): number | null {
	const margin = (Math.abs(qty) * entry * k.multiplier) / Math.max(1, leverage);
	return liquidationPrice(side, Math.abs(qty), entry, margin, k);
}

export function estimate(input: TicketInput, k: Costs): TicketEstimate {
	const { side, qty, entry, leverage, stop } = input;
	const notional = qty * k.multiplier * entry;
	const margin = notional / Math.max(1, leverage);
	const fee = notional * k.takerFee;
	const liquidation = liquidationPrice(side, qty, entry, margin, k);
	let plannedRisk: number | null = null;
	let stopBeyondLiquidation = false;
	if (stop != null && stop > 0) {
		const exitAdj = side === 'buy' ? stop * (1 - k.halfSpread - k.slippage) : stop * (1 + k.halfSpread + k.slippage);
		const perUnit = side === 'buy' ? entry - exitAdj : exitAdj - entry;
		plannedRisk = perUnit * qty * k.multiplier + fee + qty * k.multiplier * exitAdj * k.takerFee;
		if (liquidation != null) stopBeyondLiquidation = side === 'buy' ? stop <= liquidation : stop >= liquidation;
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
	for (let lev = Math.max(1, from); lev <= cap; lev++) {
		if (requiredMargin(qty, entry, lev, k) <= available) return { qty, leverage: lev };
	}
	const perUnit = requiredMargin(1, entry, cap, k);
	const fits = perUnit > 0 ? Math.floor(available / perUnit / lot + 1e-9) * lot : 0;
	return { qty: Math.min(qty, fits), leverage: cap };
}

/** How the ticket sizes an order (spec terminal-ui "Order ticket for perpetuals"). */
export type SizeBy = 'qty' | 'notional' | 'equity' | 'risk';

const floorLot = (units: number, lot: number): number => (units > 0 && Number.isFinite(units) ? Math.floor(units / lot + 1e-9) * lot : 0);

/** Quantity whose notional at `entry` is at most `notional` credits, rounded down to the lot. */
export function qtyForNotional(notional: number, entry: number, k: Costs, lot: number): number {
	if (!(notional > 0) || !(entry > 0)) return 0;
	return floorLot(notional / (entry * k.multiplier), lot);
}

/**
 * Quantity whose notional is `pct`% of equity: exposure, not margin, so 100% is one times the
 * equity whatever the leverage (above 100% needs leverage to be carried). Rounded down to the lot.
 */
export function qtyForEquityPct(equity: number, pct: number, entry: number, k: Costs, lot: number): number {
	return qtyForNotional(equity * (pct / 100), entry, k, lot);
}

export interface SizeInputs {
	equity: number;
	entry: number;
	stop: number | null;
	/** The quantity field (also used by risk sizing until a stop is set). */
	qty: number;
	/** Credits for `notional`, percent of equity for `equity`, percent risked for `risk`. */
	amount: number;
}

/** The ticket's quantity for a sizing mode, rounded down to the lot. */
export function sizedQty(by: SizeBy, x: SizeInputs, k: Costs, lot: number): number {
	if (by === 'notional') return qtyForNotional(x.amount, x.entry, k, lot);
	if (by === 'equity') return qtyForEquityPct(x.equity, x.amount, x.entry, k, lot);
	if (by === 'risk' && x.stop != null) return x.amount > 0 ? qtyForRisk(x.equity, x.amount, x.entry, x.stop, k, lot) : 0;
	return floorLot(x.qty, lot);
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
