// What-if lab view model (add-academy-and-strategies design D4, options-whatif spec): slider state,
// the engine requests it sends and the geometry of the payoff chart. Every value, P&L, Greek,
// breakeven and bound shown comes from the engine (tl_option_whatif / tl_run_option_whatif); this
// module only turns slider positions into requests and engine strings into drawing coordinates.

import type { WhatIfLegInput, WhatIfLegView, WhatIfRequest, WhatIfResult } from '#lib/engine/types.ts';
import { fmtPrice } from './format.ts';
import type { Locale } from './i18n.svelte.ts';

export type Slider = 'price' | 'time' | 'iv';
export const SLIDERS: readonly Slider[] = ['price', 'time', 'iv'];

/** Price shift in % of the forward, time elapsed (bars in a run, days in the Academy), IV shift in points. */
export interface LabShift {
	price: number;
	time: number;
	iv: number;
}

export const ZERO_SHIFT: LabShift = { price: 0, time: 0, iv: 0 };
export const PRICE_RANGE = 30;
export const IV_RANGE = 30;
export const GRID_POINTS = 61;

export const isZero = (s: LabShift) => s.price === 0 && s.time === 0 && s.iv === 0;

/** The shifted forward, on a cent grid (the engine reads it as a decimal string). */
export function shiftedForward(forward: number, pct: number): string {
	return (forward * (1 + pct / 100)).toFixed(2);
}

function grid(forward: number): WhatIfRequest['grid'] {
	return { lo: (forward * 0.7).toFixed(2), hi: (forward * 1.3).toFixed(2), points: GRID_POINTS };
}

/** A leg of the Academy lab: an illustrative market, every field editable. */
export interface AcademyLeg {
	kind: 'call' | 'put' | 'perp';
	side: 'buy' | 'sell';
	strike: number;
	qty: number;
	/** Days to expiry (options). */
	days: number;
	/** Implied volatility in % (e.g. 60). */
	ivPct: number;
	/** Premium paid or received per unit (the perpetual: its entry price). */
	entry: number;
}

export const ACADEMY_FORWARD = 100;
const MINUTES_PER_DAY = 1440;

export function academyLegInput(l: AcademyLeg): WhatIfLegInput {
	const entry = l.entry.toFixed(2);
	if (l.kind === 'perp') return { kind: 'perp', side: l.side, qty: String(l.qty), entry };
	return {
		kind: l.kind,
		side: l.side,
		qty: String(l.qty),
		strike: l.strike.toFixed(2),
		minutesToExpiry: Math.round(l.days * MINUTES_PER_DAY),
		iv: (l.ivPct / 100).toFixed(4),
		entry
	};
}

/** Run-less request: one step is one day, a contract is one unit. */
export function academyRequest(legs: readonly AcademyLeg[], shift: LabShift, forward = ACADEMY_FORWARD): WhatIfRequest {
	return {
		forward: forward.toFixed(2),
		minutesPerStep: MINUTES_PER_DAY,
		multiplier: '1',
		legs: legs.map(academyLegInput),
		shift: { forward: shiftedForward(forward, shift.price), minutesElapsed: Math.round(shift.time * MINUTES_PER_DAY), ivPoints: String(shift.iv) },
		grid: grid(forward)
	};
}

/**
 * A lesson builder's legs as Academy legs on its synthetic chain (one expiry, one IV), entries unset
 * (the perpetual at the forward). Null when the chain lacks a strike the template needs.
 */
export function builderAcademyLegs(
	legs: readonly { kind: 'call' | 'put' | 'perp'; side: 'buy' | 'sell'; strike: number | null; qty: number }[] | null,
	chain: { forward: number; days: number; ivPct: number }
): AcademyLeg[] | null {
	if (!legs) return null;
	return legs.map((l) => ({
		kind: l.kind,
		side: l.side,
		strike: l.strike ?? 0,
		qty: l.qty,
		days: chain.days,
		ivPct: chain.ivPct,
		entry: l.kind === 'perp' ? chain.forward : 0
	}));
}

/** The legs with each option's entry at the engine's model value, rounded to the cent (a fair fill). */
export function pricedLegs(legs: readonly AcademyLeg[], priced: WhatIfResult): AcademyLeg[] {
	return legs.map((l, i) => (l.kind === 'perp' ? l : { ...l, entry: Number(Number(priced.legs[i].value).toFixed(2)) }));
}

/** The run's request: legs by series id (or the perpetual), time in bars, IV model at today's base. */
export function runRequest(legs: readonly WhatIfLegInput[], shift: LabShift, forward: number): WhatIfRequest {
	return {
		legs: [...legs],
		shift: { forward: shiftedForward(forward, shift.price), bars: shift.time, ivPoints: String(shift.iv) },
		grid: grid(forward)
	};
}

/** The longest time shift that still means something: the nearest option expiry. */
export function academyMaxDays(legs: readonly AcademyLeg[]): number {
	const days = legs.filter((l) => l.kind !== 'perp').map((l) => l.days);
	return days.length ? Math.max(0, Math.min(...days)) : 0;
}

export function runMaxBars(expirySteps: readonly number[], step: number): number {
	return expirySteps.length ? Math.max(0, Math.min(...expirySteps) - step) : 0;
}

export function clampShift(s: LabShift, maxTime: number): LabShift {
	const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, Math.round(v)));
	return { price: clamp(s.price, -PRICE_RANGE, PRICE_RANGE), time: clamp(s.time, 0, maxTime), iv: clamp(s.iv, -IV_RANGE, IV_RANGE) };
}

/** Moves only the unlocked sliders (a lesson can fix the others). */
export function setSlider(s: LabShift, which: Slider, value: number, unlocked: readonly Slider[]): LabShift {
	return unlocked.includes(which) ? { ...s, [which]: value } : s;
}

// ---- payoff chart geometry ----

export interface ChartGeometry {
	expiryPath: string | null;
	todayPath: string;
	zeroY: number;
	forwardX: number;
	breakevenX: number[];
	strikeX: number[];
	lo: number;
	hi: number;
	maxAbs: number;
}

type Curve = readonly (readonly [string, string])[];

function path(curve: Curve, x: (p: number) => number, y: (v: number) => number): string {
	return curve.map(([p, v], i) => `${i ? 'L' : 'M'}${x(Number(p)).toFixed(1)},${y(Number(v)).toFixed(1)}`).join(' ');
}

/** SVG coordinates (width w, height h) of the engine's curves, breakevens and strikes. */
export function chartGeometry(r: WhatIfResult, w: number, h: number, strikes: readonly number[] = []): ChartGeometry | null {
	const today = r.today;
	if (today.length < 2) return null;
	const lo = Number(today[0][0]);
	const hi = Number(today[today.length - 1][0]);
	const values = [...today, ...(r.expiry?.curve ?? [])].map(([, v]) => Math.abs(Number(v)));
	const maxAbs = Math.max(1e-9, ...values);
	const x = (p: number) => ((p - lo) / (hi - lo)) * w;
	const y = (v: number) => h / 2 - (v / maxAbs) * (h / 2 - 6);
	const inside = (p: number) => p >= lo && p <= hi;
	return {
		expiryPath: r.expiry ? path(r.expiry.curve, x, y) : null,
		todayPath: path(today, x, y),
		zeroY: y(0),
		forwardX: x(Number(r.forward)),
		breakevenX: (r.expiry?.breakevens ?? []).map(Number).filter(inside).map(x),
		strikeX: strikes.filter(inside).map(x),
		lo,
		hi,
		maxAbs
	};
}

/** Strikes of the result's option legs (for the chart's markers). */
export function strikesOf(r: WhatIfResult): number[] {
	return [...new Set(r.legs.flatMap((l) => (l.strike == null ? [] : [Number(l.strike)])))].sort((a, b) => a - b);
}

/** Which way an unbounded loss runs: always above the upper breakeven (prices stop at 0). */
export function unboundedAbove(r: WhatIfResult): string | null {
	const e = r.expiry;
	if (!e || e.maxLoss != null) return null;
	return e.breakevens.at(-1) ?? null;
}

/**
 * A leg's row label: side, quantity as held (47.9 stays 47.9, 1 stays 1, never rounded to a whole
 * contract), then the option or the perpetual's name (`perpName`).
 */
export function legLabel(l: Pick<WhatIfLegView, 'kind' | 'side' | 'qty' | 'strike'>, locale: Locale, perpName: string): string {
	const side = l.side === 'buy' ? '+' : '−';
	const qty = new Intl.NumberFormat(locale, { maximumFractionDigits: 8 }).format(Number(l.qty));
	if (l.kind === 'perp') return `${side}${qty} ${perpName}`;
	return `${side}${qty} ${l.kind === 'call' ? 'C' : 'P'} ${fmtPrice(l.strike, locale)}`;
}
