// Expiry payoff of a lesson position (one to four option legs and an optional perpetual leg) and its
// exact analytics on the piecewise-linear payoff: breakevens, maximum profit and maximum loss (null =
// unbounded). Prices cannot go below 0, so a long perpetual's or a short put's loss is finite.
// Expiry payoffs are arithmetic (intrinsic value minus premium), not model prices: they do not need
// the pricing kernel. Fees are left out, as the lessons say.

import type { PayoffLeg } from './academy.ts';

const sign = (side: 'buy' | 'sell') => (side === 'buy' ? 1 : -1);

/** P&L of one leg at expiry for settlement price s, premium or entry included. */
export function legPnlAt(leg: PayoffLeg, s: number): number {
	if (leg.kind === 'perp') return sign(leg.side) * (s - leg.price) * leg.qty;
	const k = leg.strike ?? 0;
	const intrinsic = leg.kind === 'call' ? Math.max(0, s - k) : Math.max(0, k - s);
	return sign(leg.side) * (intrinsic - leg.price) * leg.qty;
}

export function pnlAt(legs: readonly PayoffLeg[], s: number): number {
	return legs.reduce((sum, l) => sum + legPnlAt(l, s), 0);
}

/** Slope of the payoff above every strike (per unit of price). */
function slopeAbove(legs: readonly PayoffLeg[]): number {
	return legs.reduce((sum, l) => sum + (l.kind === 'put' ? 0 : sign(l.side) * l.qty), 0);
}

/** The price 0 and every strike, ascending: the payoff is linear between them. */
function kinks(legs: readonly PayoffLeg[]): number[] {
	const strikes = legs.filter((l) => l.kind !== 'perp').map((l) => l.strike ?? 0);
	return [...new Set([0, ...strikes])].sort((a, b) => a - b);
}

const round = (v: number) => Math.round(v * 1e8) / 1e8;

/** The root of the payoff inside the segment from kink a (to b, or beyond the last strike when null), if any. */
function rootIn(legs: readonly PayoffLeg[], a: number, b: number | null): number | null {
	const fa = pnlAt(legs, a);
	const slope = b === null ? slopeAbove(legs) : (pnlAt(legs, b) - fa) / (b - a);
	if (slope === 0) return null;
	const root = a - fa / slope;
	return root > a && (b === null || root < b) ? root : null;
}

/** Prices where the expiry P&L crosses zero, ascending. */
export function breakevens(legs: readonly PayoffLeg[]): number[] {
	const xs = kinks(legs);
	const out: number[] = [];
	xs.forEach((a, i) => {
		if (a > 0 && pnlAt(legs, a) === 0) out.push(a);
		const root = rootIn(legs, a, xs[i + 1] ?? null);
		if (root !== null) out.push(root);
	});
	return [...new Set(out.map(round))];
}

/** Largest expiry profit; null when it grows without bound as the price rises. */
export function maxProfit(legs: readonly PayoffLeg[]): number | null {
	if (slopeAbove(legs) > 0) return null;
	return round(Math.max(...kinks(legs).map((x) => pnlAt(legs, x))));
}

/** Largest expiry loss as a positive amount (0 when none); null when unbounded as the price rises. */
export function maxLoss(legs: readonly PayoffLeg[]): number | null {
	if (slopeAbove(legs) < 0) return null;
	return round(Math.max(0, -Math.min(...kinks(legs).map((x) => pnlAt(legs, x)))));
}

/** Points of the expiry curve between lo and hi (strikes included, so the corners are exact). */
export function payoffCurve(legs: readonly PayoffLeg[], lo: number, hi: number, points = 60): { s: number; pnl: number }[] {
	const xs = new Set<number>();
	for (let i = 0; i <= points; i++) xs.add(lo + ((hi - lo) * i) / points);
	for (const k of kinks(legs)) if (k > lo && k < hi) xs.add(k);
	return [...xs].sort((a, b) => a - b).map((s) => ({ s, pnl: pnlAt(legs, s) }));
}
