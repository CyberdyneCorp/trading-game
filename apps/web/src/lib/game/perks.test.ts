import { describe, expect, it } from 'vitest';
import { game } from './fixtures.test-util.ts';
import { newsArrived } from './news.ts';
import { distinctFinished, newPerks, perkOptions, perksUsed, unlockedPerks, withIntel } from './perks.ts';

const BUILT_IN = ['m01-false-breakout', 'm02-funding-squeeze', 'm03-volatility-shock', 'm04-options-buyer', 'm05-options-writer'];

describe('intel perks', () => {
	it('unlock at 1, 2 and 4 distinct finished missions', () => {
		expect(unlockedPerks(0)).toEqual([]);
		expect(unlockedPerks(1)).toEqual(['calendar']);
		expect(unlockedPerks(2)).toEqual(['calendar', 'wire1']);
		expect(unlockedPerks(3)).toEqual(['calendar', 'wire1']);
		expect(unlockedPerks(4)).toEqual(['calendar', 'wire1', 'wire2']);
	});

	it('count distinct built-in missions only (repeats and imported packs do not count)', () => {
		expect(distinctFinished(['m01-false-breakout', 'm01-false-breakout', 'csv-abc'], BUILT_IN)).toBe(1);
		expect(distinctFinished(['m01-false-breakout', 'm03-volatility-shock'], BUILT_IN)).toBe(2);
	});

	it('become start options: the calendar flag and the longest enabled wire', () => {
		expect(perkOptions([])).toEqual({ calendar: false, newsLead: 0 });
		expect(perkOptions(['calendar'])).toEqual({ calendar: true, newsLead: 0 });
		expect(perkOptions(['wire1'])).toEqual({ calendar: false, newsLead: 1 });
		expect(perkOptions(['calendar', 'wire1', 'wire2'])).toEqual({ calendar: true, newsLead: 2 });
		expect(perkOptions(['wire2'])).toEqual({ calendar: false, newsLead: 2 });
	});

	it('label a run "with intel" from its start options', () => {
		expect(perksUsed({ calendar: true, newsLead: 2 })).toEqual(['calendar', 'wire2']);
		expect(perksUsed({ calendar: false, newsLead: 1 })).toEqual(['wire1']);
		expect(perksUsed(undefined)).toEqual([]);
		expect(withIntel({ calendar: false, newsLead: 0 })).toBe(false);
		expect(withIntel(undefined)).toBe(false); // runs recorded before perks existed
		expect(withIntel({ calendar: true, newsLead: 0 })).toBe(true);
		expect(withIntel({ calendar: false, newsLead: 1 })).toBe(true);
	});

	it('are introduced once: only unlocked perks not explained yet', () => {
		expect(newPerks(['calendar', 'wire1'], ['calendar'])).toEqual(['wire1']);
		expect(newPerks(['calendar'], ['calendar'])).toEqual([]);
	});
});

describe('news arrival (auto-advance pause)', () => {
	const item = { kind: 'rumor_denied' as const, tone: -1, credibility: 85, expectation: 1, actual: -1 };
	const base = game([]);
	const at = (step: number, newsLead: number, early: boolean) =>
		newsArrived({ step, news: [{ ...item, step: 82, early }], advisor: { ...base.advisor, newsLead } });

	it('pauses when a headline is released or delivered early by a fast wire', () => {
		expect(at(82, 0, false)).toBe(true);
		expect(at(83, 0, false)).toBe(false);
		expect(at(81, 1, true)).toBe(true); // first visible, one bar early
		expect(at(80, 2, true)).toBe(true);
		expect(at(81, 2, true)).toBe(false); // still early, already seen
		expect(at(82, 1, false)).toBe(true); // public release
	});
});
