// View-model of the lesson ticket sandbox (design D2 `ticket`): a perpetual ticket that never sends
// anything. Its numbers come from the terminal's own display mirrors (sizing.ts, stops.ts, tested
// against engine vectors) on a contract without fees, spread or slippage, so a lesson's arithmetic
// (risk = distance to the stop × quantity) reads exactly; the widget says costs are left out.

import { estimate, type Costs } from '#lib/game/sizing.ts';
import { rewardRisk } from '#lib/game/stops.ts';
import type { TicketField, TicketOutput, TicketWidget } from './academy.ts';

export interface SandboxInputs {
	side: 'buy' | 'sell';
	qty: number;
	leverage: number;
	stop: number | null;
	target: number | null;
}

export type SandboxOutputs = Record<TicketOutput, number | null> & { stopBeyondLiquidation: boolean };

export const sandboxCosts = (mmr: number): Costs => ({ multiplier: 1, takerFee: 0, maintenanceMargin: mmr, halfSpread: 0, slippage: 0 });

export function initialInputs(w: TicketWidget): SandboxInputs {
	return { side: w.side, qty: w.qty, leverage: w.leverage, stop: w.stop, target: w.target };
}

export function sandboxOutputs(w: TicketWidget, x: SandboxInputs): SandboxOutputs {
	const qty = Math.max(0, x.qty);
	const leverage = Math.min(w.maxLeverage, Math.max(1, x.leverage));
	const e = estimate({ side: x.side, qty, entry: w.entry, leverage, stop: x.stop }, sandboxCosts(w.mmr));
	const risk = e.plannedRisk;
	return {
		notional: e.notional,
		margin: e.margin,
		liquidation: qty > 0 ? e.liquidation : null,
		riskAtStop: risk,
		riskPct: risk == null ? null : (risk / w.equity) * 100,
		rTarget: rewardRisk(x.side, w.entry, x.stop, x.target),
		stopBeyondLiquidation: e.stopBeyondLiquidation
	};
}

/** The value a `set` question reads: an input or an output of the sandbox. */
export function sandboxValue(field: TicketField | TicketOutput, x: SandboxInputs, out: SandboxOutputs): number | null {
	if (field === 'side') return x.side === 'buy' ? 1 : -1;
	if (field in x) return x[field as Exclude<TicketField, 'side'>];
	return out[field as TicketOutput];
}

export function reachesTarget(value: number | null, target: number, tolerance: number): boolean {
	return value != null && Number.isFinite(value) && Math.abs(value - target) <= tolerance + 1e-9;
}
