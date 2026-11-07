import { describe, expect, it } from 'vitest';
import { addDays, applyReview, dueReviews, localDay, readReviews, RETIRED, scheduleOnComplete, type LessonReviews } from './review.ts';
import { EMPTY_PROGRESS, migrateProgress } from './storage.ts';

const DAY = '2026-03-10';

describe('spaced review (design D9)', () => {
	it('works in local calendar days, across month ends', () => {
		expect(localDay(new Date(2026, 2, 10, 23, 59))).toBe('2026-03-10');
		expect(addDays('2026-01-31', 1)).toBe('2026-02-01');
		expect(addDays('2026-12-30', 3)).toBe('2027-01-02');
	});

	it('Review due: a completed lesson is due one day later, not before', () => {
		const r = scheduleOnComplete({}, 'futures-pnl', DAY);
		expect(r['futures-pnl']).toEqual({ box: 1, due: '2026-03-11' });
		expect(dueReviews(r, DAY)).toEqual([]);
		expect(dueReviews(r, '2026-03-11')).toEqual(['futures-pnl']);
		expect(dueReviews(r, '2026-03-20')).toEqual(['futures-pnl']); // still due until reviewed
	});

	it('a retake keeps the schedule it already has', () => {
		const r: LessonReviews = { a: { box: 3, due: '2026-03-17' } };
		expect(scheduleOnComplete(r, 'a', DAY)).toBe(r);
	});

	it('all right on the first try moves up 3, 7, then 21 days, then retires the lesson', () => {
		let r = scheduleOnComplete({}, 'a', DAY);
		r = applyReview(r, 'a', true, '2026-03-11');
		expect(r.a).toEqual({ box: 2, due: '2026-03-14' });
		r = applyReview(r, 'a', true, '2026-03-14');
		expect(r.a).toEqual({ box: 3, due: '2026-03-21' });
		r = applyReview(r, 'a', true, '2026-03-21');
		expect(r.a).toEqual({ box: 4, due: '2026-04-11' });
		r = applyReview(r, 'a', true, '2026-04-11');
		expect(r.a.box).toBe(RETIRED);
		expect(dueReviews(r, '2030-01-01')).toEqual([]);
	});

	it('Review failed: any wrong first answer sends the lesson back to one day', () => {
		const r = applyReview({ a: { box: 3, due: DAY } }, 'a', false, DAY);
		expect(r.a).toEqual({ box: 1, due: '2026-03-11' });
	});

	it('lists due lessons in the given order and ignores unknown ids', () => {
		const r: LessonReviews = { b: { box: 1, due: DAY }, a: { box: 2, due: '2026-03-01' }, c: { box: 1, due: '2026-04-01' } };
		expect(dueReviews(r, DAY, ['a', 'b', 'c', 'x'])).toEqual(['a', 'b']);
	});

	it('progress v2 → v3 keeps everything and adds an empty schedule, no tours and no terms seen', () => {
		const v2 = { version: 2, tutorialsSeen: ['m01-false-breakout'], lessonsDone: ['foundations-bar'], advisorPoints: 4 };
		expect(migrateProgress(v2)).toEqual({
			...EMPTY_PROGRESS,
			tutorialsSeen: ['m01-false-breakout'],
			lessonsDone: ['foundations-bar'],
			advisorPoints: 4
		});
		const v3 = migrateProgress({ lessonReviews: { a: { box: 2, due: DAY }, b: { box: 'x' }, c: { box: 1, due: 'soon' } }, coachDone: ['first-trade', 3] });
		expect(v3.lessonReviews).toEqual({ a: { box: 2, due: DAY } });
		expect(v3.coachDone).toEqual(['first-trade']);
		expect(readReviews([1, 2])).toEqual({});
	});
});
