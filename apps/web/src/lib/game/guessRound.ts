// "Guess the next move" rounds (add-practice-and-advanced-strategies design D19). A round draws a round
// market, a seed and a decision step, shows the chart up to that bar, takes the player's commitment
// (long, short or no trade; the probability that the close 12 bars later is higher; for a trade an
// invalidation and a target), then reveals the next 12 bars. The score judges the decision, never the
// P&L: calibration 50, plan 40, consistency 10, kept in hundredths with integer arithmetic. Prices and
// ATR are the engine's decimal strings, compared in fixed point at 1e-8 (BigInt), never as floats.
// The desktop ports this module with the parity fixture guess.json.

import { fnv1a32, mulberry32, pick } from './prng.ts';

export type GuessDecision = 'long' | 'short' | 'none';

/** Bars revealed after the decision. */
export const GUESS_HORIZON = 12;
/** The decision step is at least this many bars after the warm-up. */
export const GUESS_LEAD = 20;
/** Probabilities offered: 5% to 95% in steps of 5. */
export const GUESS_P_MIN = 5;
export const GUESS_P_MAX = 95;
export const GUESS_P_STEP = 5;
/** The calibration table appears after this many rounds. */
export const GUESS_TABLE_AFTER = 20;

export interface RoundMarket {
	id: string;
	totalBars: number;
	warmupBars: number;
}

export interface RoundPick {
	market: string;
	seed: number;
	step: number;
}

/**
 * The market, seed and decision step of a round: mulberry32 seeded by FNV-1a 32 of
 * "guess-v1:" + date + ":" + round index; the step in [warm-up + 20, total − 13].
 */
export function pickRound(markets: readonly RoundMarket[], date: string, index: number): RoundPick {
	const next = mulberry32(fnv1a32(`guess-v1:${date}:${index}`));
	const m = markets[pick(next, markets.length)];
	const seed = next() || 1;
	const lo = m.warmupBars + GUESS_LEAD;
	const hi = m.totalBars - 1 - GUESS_HORIZON;
	return {
		market: m.id,
		seed,
		step: lo + pick(next, Math.max(1, hi - lo + 1))
	};
}

const SCALE = 100_000_000n;

/** A decimal string as an integer of 1e-8 units (extra digits truncated); null when malformed. */
export function fixed(text: string): bigint | null {
	const m = /^(-)?(\d+)(?:[.,](\d+))?$/.exec(text.trim());
	if (!m) return null;
	const frac = (m[3] ?? '').slice(0, 8).padEnd(8, '0');
	const v = BigInt(m[2]) * SCALE + BigInt(frac);
	return m[1] ? -v : v;
}

const abs = (v: bigint) => (v < 0n ? -v : v);

export interface Commitment {
	decision: GuessDecision;
	/** Percent, 5..95 in steps of 5. */
	p: number;
	/** Trades only. */
	invalidation?: string;
	target?: string;
}

export interface RoundFacts {
	/** The close at the decision bar. */
	close: string;
	/** ATR(14) at the decision bar. */
	atr: string;
	/** Was the close 12 bars later above the decision close. */
	higher: boolean;
}

export interface GuessScore {
	/** Hundredths of a point (9600 = 96). */
	calibration: number;
	plan: number;
	consistency: number;
	total: number;
	/** Plan parts for trades (null for no trade). */
	sideOk: boolean | null;
	distanceOk: boolean | null;
	rewardOk: boolean | null;
}

/** 50 − (p − o)² / 100, floored at 0, in hundredths: 5000 − (p − o)². */
export function calibrationPart(p: number, higher: boolean): number {
	const o = higher ? 100 : 0;
	return Math.max(0, 5000 - (p - o) * (p - o));
}

/** The consistency part: a long with p ≥ 55, a short with p ≤ 45, no trade with 40 ≤ p ≤ 60. */
export function consistent(decision: GuessDecision, p: number): boolean {
	if (decision === 'long') return p >= 55;
	if (decision === 'short') return p <= 45;
	return p >= 40 && p <= 60;
}

interface PlanParts {
	sideOk: boolean;
	distanceOk: boolean;
	rewardOk: boolean;
}

/**
 * A trade's plan: the invalidation on the losing side (10); its distance from the close between 0.5 and
 * 3 ATR (15); reward to risk of at least 1.5 with the target on the winning side (15). The distance and
 * the reward need the invalidation on the losing side.
 */
export function planParts(c: Commitment, f: RoundFacts): PlanParts {
	const close = fixed(f.close);
	const atr = fixed(f.atr);
	const inv = fixed(c.invalidation ?? '');
	const target = fixed(c.target ?? '');
	if (close == null || atr == null || inv == null) return { sideOk: false, distanceOk: false, rewardOk: false };
	const dir = c.decision === 'long' ? 1n : -1n;
	const risk = dir * (close - inv);
	const sideOk = risk > 0n;
	const distanceOk = sideOk && 2n * risk >= atr && risk <= 3n * atr;
	const reward = target == null ? 0n : dir * (target - close);
	const rewardOk = sideOk && reward > 0n && 2n * reward >= 3n * risk;
	return { sideOk, distanceOk, rewardOk };
}

/** The round's score in hundredths: calibration + plan + consistency. */
export function scoreRound(c: Commitment, f: RoundFacts): GuessScore {
	const calibration = calibrationPart(c.p, f.higher);
	const consistency = consistent(c.decision, c.p) ? 1000 : 0;
	if (c.decision === 'none') {
		const plan = c.p >= 40 && c.p <= 60 ? 4000 : 0;
		return {
			calibration,
			plan,
			consistency,
			total: calibration + plan + consistency,
			sideOk: null,
			distanceOk: null,
			rewardOk: null
		};
	}
	const parts = planParts(c, f);
	const plan = (parts.sideOk ? 1000 : 0) + (parts.distanceOk ? 1500 : 0) + (parts.rewardOk ? 1500 : 0);
	return {
		calibration,
		plan,
		consistency,
		total: calibration + plan + consistency,
		...parts
	};
}

export type FirstHit = 'invalidation' | 'target' | 'both' | 'neither';

/**
 * Which level the next bars reached first (information only, never scored): a bar's low or high
 * against the invalidation and the target; 'both' when one bar reached both.
 */
export function firstHit(c: Commitment, bars: readonly { high: string; low: string }[]): FirstHit {
	if (c.decision === 'none') return 'neither';
	const inv = fixed(c.invalidation ?? '');
	const target = fixed(c.target ?? '');
	const long = c.decision === 'long';
	for (const b of bars) {
		const hi = fixed(b.high);
		const lo = fixed(b.low);
		if (hi == null || lo == null) continue;
		const stop = inv != null && (long ? lo <= inv : hi >= inv);
		const tp = target != null && (long ? hi >= target : lo <= target);
		if (stop && tp) return 'both';
		if (stop) return 'invalidation';
		if (tp) return 'target';
	}
	return 'neither';
}

export interface CalibrationRow {
	/** Bucket start in percent: 0, 10, … 90 (95 falls in 90). */
	from: number;
	rounds: number;
	/** Mean stated probability in the bucket (percent). */
	stated: number;
	/** Share of rounds whose close was higher (percent). */
	observed: number;
}

/** Stated probability against observed frequency per 10% bucket (only buckets with rounds). */
export function calibrationTable(rounds: readonly { p: number; higher: boolean }[]): CalibrationRow[] {
	const rows: CalibrationRow[] = [];
	for (let from = 0; from < 100; from += 10) {
		const inside = rounds.filter((r) => Math.min(90, Math.floor(r.p / 10) * 10) === from);
		if (!inside.length) continue;
		const stated = inside.reduce((a, r) => a + r.p, 0) / inside.length;
		const observed = (100 * inside.filter((r) => r.higher).length) / inside.length;
		rows.push({ from, rounds: inside.length, stated, observed });
	}
	return rows;
}

/** A score in hundredths as points ("96", "46.25"). */
export const scorePoints = (h: number): number => h / 100;

/** The probabilities offered, ascending. */
export function probabilities(): number[] {
	const out: number[] = [];
	for (let p = GUESS_P_MIN; p <= GUESS_P_MAX; p += GUESS_P_STEP) out.push(p);
	return out;
}
