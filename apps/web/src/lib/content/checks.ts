// Correct numbers by construction (design D2): a lesson or glossary example declares each number its
// text states as a check, and the unit tests recompute every check here. The arithmetic kinds use the
// web's display mirrors (sizing.ts for liquidation); the expiry option kinds use content/payoff.ts.
// Option model values and Greeks come only from the engine: a `whatif` check names a step's `whatif`
// widget, a slider position and a field of the engine's result. bindings/python/tests/
// test_academy_numbers.py checks them (and the expiry kinds) through the Python binding's
// option_whatif, and content/academyEngine.test.ts through the WASM build (whatifCheckRequest).

import type { WhatIfRequest } from '#lib/engine/types.ts';
import { liquidationPrice } from '#lib/game/sizing.ts';
import { academyRequest, ZERO_SHIFT, type AcademyLeg, type LabShift } from '#lib/game/whatifLab.ts';
import type { PayoffLeg } from './academy.ts';
import { breakevens, maxLoss, maxProfit, pnlAt } from './payoff.ts';
import { sandboxCosts } from './ticketSandbox.ts';
import { trailTrigger } from '#lib/game/stops.ts';

type Side = 'buy' | 'sell';
interface Base {
	/** Decimals the stated value is rounded to (default 8). */
	digits?: number;
}

export type Check = Base &
	(
		| { kind: 'pnl'; side: Side; entry: number; exit: number; qty: number; expect: number }
		| { kind: 'return'; from: number; to: number; expect: number }
		| { kind: 'diff'; a: number; b: number; expect: number }
		| { kind: 'product'; a: number; b: number; expect: number }
		| { kind: 'ratio'; a: number; b: number; expect: number }
		| { kind: 'share'; a: number; b: number; expect: number }
		| { kind: 'size'; equity: number; riskPct: number; entry: number; stop: number; expect: number }
		| { kind: 'risk'; entry: number; stop: number; qty: number; expect: number }
		| { kind: 'r'; entry: number; stop: number; target: number; expect: number }
		| { kind: 'liquidation'; side: Side; entry: number; leverage: number; mmr: number; expect: number }
		| { kind: 'leverage'; notional: number; margin: number; expect: number }
		| { kind: 'funding'; notional: number; ratePct: number; expect: number }
		| { kind: 'expectancy'; winPct: number; avgWin: number; avgLoss: number; expect: number }
		| { kind: 'mean'; values: number[]; expect: number }
		| { kind: 'recovery'; drawdownPct: number; expect: number }
		| { kind: 'streak'; lossPct: number; n: number; expect: number }
		| { kind: 'roundTrip'; bid: number; ask: number; qty: number; expect: number }
		| { kind: 'payoff'; legs: PayoffLeg[]; at: number; expect: number }
		| { kind: 'breakeven'; legs: PayoffLeg[]; expect: number[] }
		| { kind: 'maxProfit'; legs: PayoffLeg[]; expect: number | null }
		| { kind: 'maxLoss'; legs: PayoffLeg[]; expect: number | null }
		| TrailCheck
		| WhatIfCheck
	);

/**
 * An engine number: the step `step`'s what-if widget with its sliders at `shift`, valued by
 * tl_option_whatif; `field` is a path into the result ("legs.0.value", "net.pnl", "expiry.maxLoss").
 */
export interface WhatIfCheck {
	kind: 'whatif';
	step: string;
	shift?: Partial<LabShift>;
	field: string;
	expect: number;
}

/**
 * A trailing stop's trigger for a best close (design D10): the stop's side (sell = a long's stop), a
 * fixed distance or a percent of the close, and the tick; exactly the engine's rounding (stops.ts).
 */
export interface TrailCheck {
	kind: 'trail';
	side: Side;
	close: number;
	distance?: number;
	ratePct?: number;
	tick: number;
	expect: number;
}

/** The trigger a trail check computes, through the exact fixed-point mirror of the engine. */
export function trailOf(c: TrailCheck): number {
	const rate = c.ratePct != null ? (c.ratePct / 100).toFixed(10) : undefined;
	const distance = c.distance != null ? c.distance.toFixed(8) : undefined;
	return Number(trailTrigger(c.side, c.close.toFixed(8), { rate, distance }, c.tick.toFixed(8)));
}

/** Kinds only the engine computes (verified by the engine tests, not by computeCheck). */
export const isEngineCheck = (c: Check): c is Base & WhatIfCheck => c.kind === 'whatif';

/** The request a `whatif` check values: exactly what the step's widget sends at that slider position. */
export function whatifCheckRequest(c: WhatIfCheck, widget: { forward: number; legs: AcademyLeg[] }): WhatIfRequest {
	return academyRequest(widget.legs, { ...ZERO_SHIFT, ...c.shift }, widget.forward);
}

/** A field of an engine result by its path; null when absent. */
export function fieldOf(result: unknown, path: string): number | null {
	const v = path.split('.').reduce<unknown>((o, k) => (o != null && typeof o === 'object' ? (o as Record<string, unknown>)[k] : undefined), result);
	return v == null ? null : Number(v);
}

const roundTo = (v: number, digits: number) => Math.round(v * 10 ** digits) / 10 ** digits;
const dir = (side: Side) => (side === 'buy' ? 1 : -1);

function scalar(c: Check): number | null {
	switch (c.kind) {
		case 'pnl':
			return dir(c.side) * (c.exit - c.entry) * c.qty;
		case 'return':
			return ((c.to - c.from) / c.from) * 100;
		case 'diff':
			return c.a - c.b;
		case 'product':
			return c.a * c.b;
		case 'ratio':
			return c.a / c.b;
		case 'share':
			return (c.a / c.b) * 100;
		case 'size':
			return (c.equity * c.riskPct) / 100 / Math.abs(c.entry - c.stop);
		case 'risk':
			return Math.abs(c.entry - c.stop) * c.qty;
		case 'r':
			return Math.abs(c.target - c.entry) / Math.abs(c.entry - c.stop);
		case 'liquidation':
			return liquidationPrice(c.side, 1, c.entry, c.entry / c.leverage, sandboxCosts(c.mmr));
		case 'leverage':
			return c.notional / c.margin;
		case 'funding':
			return (c.notional * c.ratePct) / 100;
		case 'expectancy':
			return (c.winPct / 100) * c.avgWin - (1 - c.winPct / 100) * c.avgLoss;
		case 'mean':
			return c.values.reduce((a, b) => a + b, 0) / c.values.length;
		case 'recovery':
			return (1 / (1 - c.drawdownPct / 100) - 1) * 100;
		case 'streak':
			return (c.lossPct / 100) ** c.n * 100;
		case 'roundTrip':
			return (c.ask - c.bid) * c.qty;
		case 'payoff':
			return pnlAt(c.legs, c.at);
		case 'maxProfit':
			return maxProfit(c.legs);
		case 'maxLoss':
			return maxLoss(c.legs);
		case 'trail':
			return trailOf(c);
		default:
			return null;
	}
}

/** The value the check computes (breakevens: a list), rounded to its digits. */
export function computeCheck(c: Check): number | number[] | null {
	const digits = c.digits ?? 8;
	if (c.kind === 'breakeven') return breakevens(c.legs).map((v) => roundTo(v, digits));
	const v = scalar(c);
	return v == null ? null : roundTo(v, digits);
}

/** Null when the stated value matches the computed one, else a message naming both (engine kinds: see isEngineCheck). */
export function checkProblem(c: Check): string | null {
	if (isEngineCheck(c)) return null;
	const got = computeCheck(c);
	const same = JSON.stringify(got) === JSON.stringify(c.expect);
	return same ? null : `${c.kind}: states ${JSON.stringify(c.expect)}, computed ${JSON.stringify(got)}`;
}

/** Every number a value carries (recursively), for the "no unchecked number" test. */
export function numbersIn(v: unknown, out: number[] = []): number[] {
	if (typeof v === 'number') out.push(v);
	else if (Array.isArray(v)) for (const x of v) numbersIn(x, out);
	else if (v && typeof v === 'object') for (const x of Object.values(v)) numbersIn(x, out);
	return out;
}
