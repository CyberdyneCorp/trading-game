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

/**
 * Margin check for the part of an order that opens or adds to the position, as the engine does it:
 * initial margin (notional / leverage) plus the taker fee must fit in the available balance.
 * Returns null when it fits; otherwise what is needed and the lowest leverage up to the cap that
 * would fit (null when even the cap does not).
 */
export function marginShortfall(
	incQty: number,
	entry: number,
	leverage: number,
	available: number,
	cap: number,
	k: Costs
): { need: number; minLeverage: number | null } | null {
	const notional = incQty * k.multiplier * entry;
	const fee = notional * k.takerFee;
	const needAt = (lev: number) => notional / Math.max(1, lev) + fee;
	if (incQty <= 0 || needAt(leverage) <= available) return null;
	let minLeverage: number | null = null;
	for (let l = leverage + 1; l <= cap && minLeverage == null; l++) if (needAt(l) <= available) minLeverage = l;
	return { need: needAt(leverage), minLeverage };
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
