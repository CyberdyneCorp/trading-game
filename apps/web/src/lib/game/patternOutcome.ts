// What usually comes after a candle pattern, for the cheat sheet animation (spec terminal-ui
// "Candle pattern cheat sheet"). Schematic, like the patterns themselves: scenario A is the textbook
// case (a confirmation candle closes beyond the pattern, then follow-through), scenario B is the
// failure (no confirmation, price closes through the stop). Neutral patterns break up, then down.
// Both are shown because in this simulation patterns alone are right only about half the time.

import type { Candle, CandlePattern } from './candlePatterns.ts';

export type OutcomeKind = 'confirmed' | 'failed' | 'breakUp' | 'breakDown';

export interface Outcome {
	kind: OutcomeKind;
	candles: Candle[]; // always OUTCOME_BARS candles, the first one is the confirmation (or its absence)
	level: number; // dashed line: the confirmation trigger, or the stop for a failure
	levelKind: 'trigger' | 'stop';
}

export const OUTCOME_BARS = 3;

function bar(open: number, close: number, wick: number): Candle {
	return [open, Math.max(open, close) + wick, Math.min(open, close) - wick, close];
}

/** Consecutive candles, each opening at the previous close. */
function run(start: number, closes: number[], wick: number): Candle[] {
	let open = start;
	return closes.map((close) => {
		const c = bar(open, close, wick);
		open = close;
		return c;
	});
}

/** Price moved `d` units in direction `dir` (+1 up, -1 down) from `x`. */
const away = (x: number, dir: number, d: number) => x + dir * d;

function breakout(lastClose: number, edge: number, dir: number, u: number, kind: OutcomeKind): Outcome {
	const closes = [away(edge, dir, 0.4 * u), away(edge, dir, 1.1 * u), away(edge, dir, 1.7 * u)];
	return { kind, candles: run(lastClose, closes, 0.15 * u), level: edge, levelKind: 'trigger' };
}

function failure(lastClose: number, stop: number, dir: number, u: number): Outcome {
	// dir is the pattern's direction; the failure goes the other way and closes through the stop.
	const closes = [(lastClose + stop) / 2, away(stop, -dir, 0.4 * u), away(stop, -dir, 0.9 * u)];
	return { kind: 'failed', candles: run(lastClose, closes, 0.15 * u), level: stop, levelKind: 'stop' };
}

/** The two scenarios for a pattern: [textbook or break up, failure or break down]. */
export function outcomes(p: CandlePattern): [Outcome, Outcome] {
	const hi = Math.max(...p.candles.map((c) => c[1]));
	const lo = Math.min(...p.candles.map((c) => c[2]));
	const u = Math.max((hi - lo) * 0.3, 6);
	const lastClose = p.candles[p.candles.length - 1][3];
	if (p.bias === 'neutral') return [breakout(lastClose, hi, 1, u, 'breakUp'), breakout(lastClose, lo, -1, u, 'breakDown')];
	const dir = p.bias === 'bullish' ? 1 : -1;
	const [trigger, stop] = dir > 0 ? [hi, lo] : [lo, hi];
	return [breakout(lastClose, trigger, dir, u, 'confirmed'), failure(lastClose, stop, dir, u)];
}

/** One animation frame: which scenario is drawn (null: the pattern alone) and how many of its candles. */
export interface Frame {
	scenario: 0 | 1 | null;
	shown: number;
}

function scenarioFrames(scenario: 0 | 1): Frame[] {
	const grow = Array.from({ length: OUTCOME_BARS }, (_, i) => ({ scenario, shown: i + 1 }));
	const hold = { scenario, shown: OUTCOME_BARS };
	return [{ scenario: null, shown: 0 }, ...grow, hold, hold];
}

/** The loop: pattern, scenario A candle by candle, hold; pattern, scenario B candle by candle, hold. */
export const FRAMES: Frame[] = [...scenarioFrames(0), ...scenarioFrames(1)];

/** The still picture used when motion is reduced: the textbook case, complete. */
export const STILL: Frame = { scenario: 0, shown: OUTCOME_BARS };
