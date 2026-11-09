import { beforeAll, describe, expect, it } from 'vitest';
import { daysBetween, daysFromCivil, fnv1a32, mulberry32 } from './prng.ts';
import { DAILY_POOL_V1, dailyChallenge, dailyHistory, dailySeed, recordDailyAttempt } from './daily.ts';
import {
	calibrationPart,
	calibrationTable,
	consistent,
	firstHit,
	fixed,
	GUESS_HORIZON,
	GUESS_LEAD,
	pickRound,
	probabilities,
	scoreRound,
	type Commitment
} from './guessRound.ts';
import { missionsOnly, roundMarkets } from './practice.ts';
import { EMPTY_PROGRESS, migrateProgress, type Progress } from './storage.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import type { MissionInfo } from '#lib/engine/types.ts';

describe('deterministic draws (prng.ts)', () => {
	it('FNV-1a 32 and mulberry32 match their reference vectors', () => {
		expect(fnv1a32('')).toBe(0x811c9dc5);
		expect(fnv1a32('a')).toBe(0xe40c292c);
		const next = mulberry32(1);
		// The reference mulberry32(1)'s first float 0.6270739405881613 is 2693262067 / 2^32.
		expect([next(), next(), next()]).toEqual([2693262067, 11749833, 2265367787]);
	});

	it('counts civil days, not milliseconds', () => {
		expect(daysFromCivil(1970, 1, 1)).toBe(0);
		expect(daysBetween('2026-01-01', '2026-01-01')).toBe(0);
		expect(daysBetween('2026-01-01', '2027-01-01')).toBe(365);
		expect(daysBetween('2027-12-31', '2028-03-01')).toBe(61); // 2028 is a leap year
		expect(daysBetween('2026-01-01', '2025-12-31')).toBe(-1);
		expect(Number.isNaN(daysBetween('2026-01-01', 'nope'))).toBe(true);
	});
});

describe('daily challenge (design D16)', () => {
	it('picks the pool mission by whole days from 2026-01-01 and seeds by the date', () => {
		expect(dailyChallenge('2026-01-01').mission).toBe(DAILY_POOL_V1[0]);
		expect(dailyChallenge('2026-01-02').mission).toBe(DAILY_POOL_V1[1]);
		expect(dailyChallenge(`2026-01-25`).mission).toBe(DAILY_POOL_V1[0]); // 24 missions later
		expect(dailyChallenge('2025-12-31').mission).toBe(DAILY_POOL_V1[DAILY_POOL_V1.length - 1]);
		expect(dailyChallenge('2026-11-08').seed).toBe(fnv1a32('tradelab-daily-v1:2026-11-08'));
		expect(dailySeed('2026-11-08')).toBeGreaterThan(0);
	});

	it('leaves the capstone and the round markets out of the pool', () => {
		expect(DAILY_POOL_V1).toHaveLength(24);
		expect(DAILY_POOL_V1).not.toContain('m12-capstone');
		expect(DAILY_POOL_V1.some((m) => m.startsWith('r0'))).toBe(false);
	});

	it('First and best: 62 then 75 keeps 62 as the record and 75 as the best', () => {
		const link = dailyChallenge('2026-11-08');
		let p: Progress = { ...EMPTY_PROGRESS };
		p = recordDailyAttempt(p, link, { score: 62, pnl: '-12.50', runId: 'a' });
		p = recordDailyAttempt(p, link, { score: 75, pnl: '-30.00', runId: 'b' });
		p = recordDailyAttempt(p, link, { score: 70, pnl: '80.00', runId: 'c' }); // more P&L, lower score
		const day = p.dailyChallenges['2026-11-08'];
		expect(day.first?.score).toBe(62);
		expect(day.best?.score).toBe(75); // ranked by the score, never by P&L
		expect(day.attempts).toBe(3);
		expect(recordDailyAttempt(p, link, { score: 99, pnl: '0', runId: 'a' })).toBe(p); // the same run twice
	});

	it('keeps the last 90 days, newest first, and survives the progress reader', () => {
		let p: Progress = { ...EMPTY_PROGRESS };
		for (let i = 0; i < 95; i++) {
			const d = new Date(2026, 0, 1 + i);
			const date = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
			p = recordDailyAttempt(p, dailyChallenge(date), { score: i, pnl: '0', runId: `r${i}` });
		}
		const rows = dailyHistory(p);
		expect(rows).toHaveLength(90);
		expect(rows[0].date).toBe('2026-04-05');
		expect(migrateProgress(JSON.parse(JSON.stringify(p))).dailyChallenges).toEqual(p.dailyChallenges);
	});
});

const close = '100';
const atr = '2';
const long = (p: number, inv: string, target: string): Commitment => ({ decision: 'long', p, invalidation: inv, target });

describe('guess rounds scoring (design D19)', () => {
	it('Calibrated long: p = 80, invalidation 1 ATR below, target 2 ATR above, closed higher scores 46 + 40 + 10 = 96', () => {
		const s = scoreRound(long(80, '98', '104'), { close, atr, higher: true });
		expect(s).toMatchObject({ calibration: 4600, plan: 4000, consistency: 1000, total: 9600 });
	});

	it('calibration is 50 − (p − o)² / 100, floored at 0, in hundredths', () => {
		expect(calibrationPart(50, true)).toBe(2500);
		expect(calibrationPart(50, false)).toBe(2500);
		expect(calibrationPart(80, true)).toBe(4600);
		expect(calibrationPart(80, false)).toBe(0); // 50 − 64 floored
		expect(calibrationPart(95, true)).toBe(4975);
		expect(calibrationPart(5, false)).toBe(4975);
	});

	it('P&L ignored: the target hit or not, the same commitment and outcome score the same', () => {
		const c = long(70, '98', '104');
		const hit = firstHit(c, [{ high: '104.5', low: '99' }]);
		const miss = firstHit(c, [{ high: '101', low: '99' }]);
		expect([hit, miss]).toEqual(['target', 'neither']);
		expect(scoreRound(c, { close, atr, higher: true })).toEqual(scoreRound(c, { close, atr, higher: true }));
		expect(firstHit(c, [{ high: '104.5', low: '97' }])).toBe('both');
		expect(firstHit(c, [{ high: '101', low: '97.99' }])).toBe('invalidation');
	});

	it('plan parts: the losing side, 0.5 to 3 ATR, reward to risk 1.5, compared in fixed point', () => {
		const at = (c: Commitment) => scoreRound(c, { close, atr, higher: false });
		expect(at(long(60, '101', '104')).plan).toBe(0); // invalidation above a long's close
		expect(at(long(60, '99.01', '104')).plan).toBe(1000 + 0 + 1500); // 0.99 < 1 = 0.5 ATR
		expect(at(long(60, '99', '101.5')).plan).toBe(1000 + 1500 + 1500); // exactly 0.5 ATR and exactly 1.5R
		expect(at(long(60, '99', '101.49')).plan).toBe(1000 + 1500); // just under 1.5R
		expect(at(long(60, '94', '120')).plan).toBe(1000 + 1500 + 1500); // exactly 3 ATR
		expect(at(long(60, '93.99', '120')).plan).toBe(1000 + 1500); // beyond 3 ATR
		expect(at({ decision: 'short', p: 30, invalidation: '102', target: '97' }).plan).toBe(4000);
	});

	it('no trade scores the plan 40 only with no edge (40 ≤ p ≤ 60); consistency follows the side', () => {
		expect(scoreRound({ decision: 'none', p: 50 }, { close, atr, higher: true })).toMatchObject({ plan: 4000, consistency: 1000, total: 7500 });
		expect(scoreRound({ decision: 'none', p: 70 }, { close, atr, higher: true }).plan).toBe(0);
		expect([consistent('long', 55), consistent('long', 50), consistent('short', 45), consistent('short', 50)]).toEqual([true, false, true, false]);
	});

	it('reads decimal strings exactly at 1e-8', () => {
		expect(fixed('100.12345678')).toBe(10012345678n);
		expect(fixed('99,5')).toBe(9950000000n);
		expect(fixed('-1.5')).toBe(-150000000n);
		expect(fixed('abc')).toBeNull();
	});

	it('offers 5% to 95% in steps of 5 and tables stated against observed per 10% bucket', () => {
		expect(probabilities()).toHaveLength(19);
		const table = calibrationTable([
			{ p: 70, higher: true },
			{ p: 75, higher: false },
			{ p: 95, higher: true },
			{ p: 20, higher: false }
		]);
		expect(table.map((r) => [r.from, r.rounds, r.stated, r.observed])).toEqual([
			[20, 1, 20, 0],
			[70, 2, 72.5, 50],
			[90, 1, 95, 100]
		]);
	});

	it('draws the decision step in [warm-up + 20, total − 13]', () => {
		const markets = [{ id: 'r01-trend', totalBars: 160, warmupBars: 60 }];
		for (let i = 0; i < 300; i++) {
			const r = pickRound(markets, '2026-11-08', i);
			expect(r.step).toBeGreaterThanOrEqual(60 + GUESS_LEAD);
			expect(r.step).toBeLessThanOrEqual(160 - 1 - GUESS_HORIZON);
			expect(r.seed).toBeGreaterThan(0);
		}
		expect(pickRound(markets, '2026-11-08', 3)).toEqual(pickRound(markets, '2026-11-08', 3));
	});
});

describe('round markets stay out of menus and the daily pool (design D19)', () => {
	const info = (id: string, practice?: string): MissionInfo => ({ id, totalBars: 160, warmupBars: 60, leverageCap: 3, allowShort: true, ...(practice ? { practice } : {}) });
	it('splits the catalog', () => {
		const list = [info('m01-false-breakout'), info('r01-trend', 'round')];
		expect(missionsOnly(list).map((m) => m.id)).toEqual(['m01-false-breakout']);
		expect(roundMarkets(list).map((m) => m.id)).toEqual(['r01-trend']);
	});

	describe.skipIf(!wasmBuilt)('against the engine catalog (WASM)', () => {
		let engine: NodeEngine;
		beforeAll(async () => {
			engine = await loadEngine();
		});
		it('the pool is every mission but the capstone, and the four rounds are listed as practice', () => {
			const all = engine.call<MissionInfo[]>('tl_missions', [], []);
			expect(roundMarkets(all).map((m) => m.id)).toEqual(['r01-trend', 'r02-range', 'r03-breakout', 'r04-event']);
			expect([...DAILY_POOL_V1]).toEqual(
				missionsOnly(all)
					.map((m) => m.id)
					.filter((id) => id !== 'm12-capstone')
			);
		});
	});
});
