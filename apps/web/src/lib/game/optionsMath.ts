// Display-side option helpers (payoff, breakeven, margin estimate, position Greeks).
// Estimates only: the engine is authoritative for fills, margin, margin calls and settlement.

import type { OptionQuoteView, OptionsView } from '#lib/engine/types.ts';
import { fmtPrice } from './format.ts';
import type { Locale } from './i18n.svelte.ts';

export type OptSide = 'buy' | 'sell';

export interface OptionLeg {
	kind: 'call' | 'put';
	strike: number;
	side: OptSide;
	qty: number; // contracts
	premium: number; // per unit
	multiplier: number;
}

export function legFromQuote(q: OptionQuoteView, side: OptSide, qty: number, mult: number): OptionLeg {
	return { kind: q.kind, strike: Number(q.strike), side, qty, premium: Number(side === 'buy' ? q.ask : q.bid), multiplier: mult };
}

/** P&L of the leg at expiry for settlement price s (premium included, fees excluded). */
export function payoffAt(leg: OptionLeg, s: number): number {
	const intrinsic = leg.kind === 'call' ? Math.max(0, s - leg.strike) : Math.max(0, leg.strike - s);
	const perUnit = intrinsic - leg.premium;
	return (leg.side === 'buy' ? perUnit : -perUnit) * leg.qty * leg.multiplier;
}

export function breakeven(leg: OptionLeg): number {
	return leg.kind === 'call' ? leg.strike + leg.premium : leg.strike - leg.premium;
}

/** Max loss for a buyer = premium paid (null = unbounded/large for writers). */
export function maxLoss(leg: OptionLeg): number | null {
	return leg.side === 'buy' ? leg.premium * leg.qty * leg.multiplier : null;
}

export interface MarginParams {
	imBase: number;
	imFloor: number;
	mmBase: number;
}

export const DEFAULT_MARGIN: MarginParams = { imBase: 0.15, imFloor: 0.1, mmBase: 0.075 };

/** Initial margin to write the leg (engine formula: max(imBase*F - OTM, imFloor*F) + ask, per unit). */
export function writeMargin(q: OptionQuoteView, f: number, qty: number, mult: number, p = DEFAULT_MARGIN): number {
	const k = Number(q.strike);
	const otm = Math.max(0, q.kind === 'call' ? k - f : f - k);
	const perUnit = Math.max(p.imBase * f - otm, p.imFloor * f) + Number(q.ask);
	return perUnit * qty * mult;
}

export function payoffCurve(leg: OptionLeg, lo: number, hi: number, points = 60): { s: number; pnl: number }[] {
	const out: { s: number; pnl: number }[] = [];
	for (let i = 0; i <= points; i++) {
		const s = lo + ((hi - lo) * i) / points;
		out.push({ s, pnl: payoffAt(leg, s) });
	}
	return out;
}

export interface PositionGreeks {
	delta: number;
	gamma: number;
	theta: number; // per bar
	vega: number; // per vol point
}

/** Aggregate Greeks of all option positions (signed by quantity, times multiplier). */
export function positionGreeks(opts: OptionsView): PositionGreeks {
	const g: PositionGreeks = { delta: 0, gamma: 0, theta: 0, vega: 0 };
	const mult = Number(opts.multiplier);
	for (const p of opts.positions) {
		const q = opts.chain.find((c) => c.id === p.series);
		if (!q) continue;
		const n = Number(p.qty) * mult;
		g.delta += n * Number(q.delta);
		g.gamma += n * Number(q.gamma);
		g.theta += n * Number(q.theta);
		g.vega += n * Number(q.vega);
	}
	return g;
}

/** "C 100 · exp 96"; a strike off the whole numbers keeps its decimals in the locale ("P 97,50 · exp 85"). */
export function seriesLabel(q: Pick<OptionQuoteView, 'kind' | 'strike' | 'expiryStep'>, locale: Locale): string {
	const strike = Number(q.strike);
	const shown = Number.isInteger(strike) ? String(strike) : fmtPrice(q.strike, locale);
	return `${q.kind === 'call' ? 'C' : 'P'} ${shown} · exp ${q.expiryStep}`;
}
