// Stop-loss and take-profit helpers for the order ticket (spec terminal-ui "Stop and target tools").
// Pure functions over plain numbers: the engine still validates every price it receives.

import type { Side } from './sizing.ts';

/** Bars as [open, high, low, close, volume]. */
export type BarTuple = [number, number, number, number, number];

const SWING_LOOKBACK = 10;

/** Stop `mult` ATRs away from the entry, on the losing side. */
export function stopFromAtr(side: Side, entry: number, atr: number | null, mult: number): number | null {
	if (atr == null || !(atr > 0) || !(entry > 0)) return null;
	const stop = side === 'buy' ? entry - mult * atr : entry + mult * atr;
	return stop > 0 ? stop : null;
}

/** Stop `pct` percent away from the entry, on the losing side. */
export function stopFromPct(side: Side, entry: number, pct: number): number | null {
	if (!(entry > 0) || !(pct > 0)) return null;
	const stop = side === 'buy' ? entry * (1 - pct / 100) : entry * (1 + pct / 100);
	return stop > 0 ? stop : null;
}

/**
 * Beyond the recent swing: one tick under the lowest low (long) or over the highest high (short)
 * of the last `lookback` closed bars. Null when the entry is already past that level.
 */
export function swingStop(side: Side, entry: number, bars: BarTuple[], tick: number, lookback = SWING_LOOKBACK): number | null {
	const recent = bars.slice(-lookback);
	if (recent.length === 0) return null;
	if (side === 'buy') {
		const low = Math.min(...recent.map((b) => b[2])) - tick;
		return low > 0 && low < entry ? low : null;
	}
	const high = Math.max(...recent.map((b) => b[1])) + tick;
	return high > entry ? high : null;
}

/** Target at `r` times the entry-to-stop distance, on the winning side. */
export function targetFromR(side: Side, entry: number, stop: number | null, r: number): number | null {
	if (stop == null) return null;
	const risk = Math.abs(entry - stop);
	if (!(risk > 0)) return null;
	const tp = side === 'buy' ? entry + r * risk : entry - r * risk;
	return tp > 0 ? tp : null;
}

export interface Distance {
	abs: number;
	pct: number;
	atr: number | null; // distance in ATR multiples
}

export function distanceOf(entry: number, level: number | null, atr: number | null): Distance | null {
	if (level == null || !(entry > 0) || !(level > 0)) return null;
	const abs = Math.abs(entry - level);
	return { abs, pct: (abs / entry) * 100, atr: atr != null && atr > 0 ? abs / atr : null };
}

/** Reward-to-risk ratio of a target versus a stop (null without both on their correct sides). */
export function rewardRisk(side: Side, entry: number, stop: number | null, tp: number | null): number | null {
	if (stop == null || tp == null) return null;
	const risk = side === 'buy' ? entry - stop : stop - entry;
	const reward = side === 'buy' ? tp - entry : entry - tp;
	return risk > 0 && reward > 0 ? reward / risk : null;
}

/** True when the level is on the side where it can work as a stop for this side. */
export function stopOnLosingSide(side: Side, entry: number, stop: number): boolean {
	return side === 'buy' ? stop < entry : stop > entry;
}

export function targetOnWinningSide(side: Side, entry: number, tp: number): boolean {
	return side === 'buy' ? tp > entry : tp < entry;
}
