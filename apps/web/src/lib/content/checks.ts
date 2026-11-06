// Correct numbers by construction (design D2): a lesson or glossary example declares each number its
// text states as a check, and the unit tests recompute every check here. The arithmetic kinds use the
// web's display mirrors (sizing.ts for liquidation); the expiry option kinds use content/payoff.ts.
// Option model values and Greeks are not stated by any lesson until the engine's what-if call is
// available to the client (task W5); bindings/python/tests/test_academy_numbers.py will check those.

import { liquidationPrice } from '#lib/game/sizing.ts';
import type { PayoffLeg } from './academy.ts';
import { breakevens, maxLoss, maxProfit, pnlAt } from './payoff.ts';
import { sandboxCosts } from './ticketSandbox.ts';

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
	);

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

/** Null when the stated value matches the computed one, else a message naming both. */
export function checkProblem(c: Check): string | null {
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
