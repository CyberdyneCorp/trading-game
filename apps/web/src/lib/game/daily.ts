// Daily challenge (add-practice-and-advanced-strategies design D16): the same mission and seed for
// everyone on the same local date, picked without a server. The run uses the mission's own rules, no
// advisor, no perks and the default balance; its record carries `daily`. The history keeps the first
// finished attempt of each day (the record) and the best beside it, ranked by the mission score
// (process), never by P&L. No streak, no leaderboard.

import { daysBetween, fnv1a32 } from './prng.ts';
import { DAILY_KEEP_DAYS, type DailyAttempt, type DailyChallenge, type DailyLink, type Progress } from './storage.ts';

/** Day 0 of the pool. */
export const DAILY_EPOCH = '2026-01-01';

/**
 * Version 1 of the pool: every built-in mission except the capstone (m12, whose debrief carries the
 * campaign report) and the round markets. Fixed and versioned: changing it changes everyone's
 * challenge, so a new list is DAILY_POOL_V2 with its own seed prefix.
 */
export const DAILY_POOL_V1: readonly string[] = [
	'm01-false-breakout',
	'm02-funding-squeeze',
	'm03-volatility-shock',
	'm04-options-buyer',
	'm05-options-writer',
	'm06-thin-book',
	'm07-trend-trailing',
	'm08-regime-switch',
	'm09-event-straddle',
	'm10-hedged-book',
	'm11-news-calendar',
	'm13-call-spread',
	'm14-covered-call',
	'm15-protective-put',
	'm16-range-condor',
	'm17-before-expiry',
	'm18-gamma-scalp',
	'm19-funding-carry',
	'm20-event-vol',
	'm21-calendar',
	'm22-scale-manage',
	'm23-second-chance',
	'm24-fast-market',
	'm25-the-dip'
];

/** The seed of a date's challenge: FNV-1a 32 of "tradelab-daily-v1:" + date (0 becomes 1). */
export function dailySeed(date: string): number {
	return fnv1a32(`tradelab-daily-v1:${date}`) || 1;
}

/** The challenge of a local date ("YYYY-MM-DD"): the pool's mission at dayIndex mod n, and the seed. */
export function dailyChallenge(date: string): DailyLink {
	const day = daysBetween(DAILY_EPOCH, date);
	const n = DAILY_POOL_V1.length;
	return {
		date,
		mission: DAILY_POOL_V1[((day % n) + n) % n],
		seed: dailySeed(date)
	};
}

/** Is `a` a better attempt than `b`: a higher mission score; a scored attempt beats an unscored one. */
const better = (a: DailyAttempt, b: DailyAttempt | null): boolean => !b || (a.score ?? -1) > (b.score ?? -1);

/**
 * A finished attempt of the day's challenge: the first one is the day's record and stays; the best is
 * replaced only by a higher score. Days beyond the last DAILY_KEEP_DAYS are dropped.
 */
export function recordDailyAttempt(p: Progress, link: DailyLink, attempt: DailyAttempt): Progress {
	const day: DailyChallenge = p.dailyChallenges[link.date] ?? {
		mission: link.mission,
		seed: link.seed,
		first: null,
		best: null,
		attempts: 0
	};
	if (day.first?.runId === attempt.runId || day.best?.runId === attempt.runId) return p;
	const next: DailyChallenge = {
		...day,
		first: day.first ?? attempt,
		best: better(attempt, day.best) ? attempt : day.best,
		attempts: day.attempts + 1
	};
	const all = { ...p.dailyChallenges, [link.date]: next };
	const keep = Object.keys(all).sort().slice(-DAILY_KEEP_DAYS);
	return {
		...p,
		dailyChallenges: Object.fromEntries(keep.map((d) => [d, all[d]]))
	};
}

/** The history rows, newest day first. */
export function dailyHistory(p: Progress): (DailyChallenge & { date: string })[] {
	return Object.entries(p.dailyChallenges)
		.map(([date, d]) => ({ date, ...d }))
		.sort((a, b) => (a.date < b.date ? 1 : -1));
}
