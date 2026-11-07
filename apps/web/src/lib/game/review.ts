// Light spaced review of Academy lessons (add-academy-and-strategies design D9, academy spec
// "Completion, retakes and spaced review"). Completing a lesson schedules a review of its decision and
// transfer questions 1 day later (box 1). A review answered all right on the first try moves the lesson
// up a box (3, 7, then 21 days); after the 21-day box it is retired. Any wrong first answer sends it
// back to box 1. Dates are the device's local calendar days ("YYYY-MM-DD"); nothing here reaches the
// engine or any run.

/** Days until the review of each box (box 1 = the first review). */
export const REVIEW_DAYS = [1, 3, 7, 21] as const;

export interface ReviewEntry {
	/** 1–4 while scheduled; RETIRED = no more reviews. */
	box: number;
	/** Local calendar day the review is due. */
	due: string;
}

export type LessonReviews = Record<string, ReviewEntry>;

export const RETIRED = REVIEW_DAYS.length + 1;

/** Local calendar day of a time ("YYYY-MM-DD"). */
export function localDay(time: number | Date): string {
	const d = new Date(time);
	const pad = (n: number) => String(n).padStart(2, '0');
	return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

/** `day` plus `n` calendar days. */
export function addDays(day: string, n: number): string {
	const [y, m, d] = day.split('-').map(Number);
	return localDay(new Date(y, m - 1, d + n));
}

const entry = (box: number, today: string): ReviewEntry => ({ box, due: addDays(today, REVIEW_DAYS[box - 1]) });

/** A completed lesson: scheduled for box 1 the first time; a retake leaves an existing schedule as it is. */
export function scheduleOnComplete(reviews: LessonReviews, id: string, today: string): LessonReviews {
	return reviews[id] ? reviews : { ...reviews, [id]: entry(1, today) };
}

/** A finished review: up a box (retired after the last) when all were right on the first try, else box 1. */
export function applyReview(reviews: LessonReviews, id: string, allRight: boolean, today: string): LessonReviews {
	if (!allRight) return { ...reviews, [id]: entry(1, today) };
	const next = (reviews[id]?.box ?? 1) + 1;
	return { ...reviews, [id]: next >= RETIRED ? { box: RETIRED, due: today } : entry(next, today) };
}

export const isRetired = (e: ReviewEntry | undefined): boolean => !!e && e.box >= RETIRED;

/** Lesson ids whose review is due on `today` or earlier, in the given order (default: the schedule's). */
export function dueReviews(reviews: LessonReviews, today: string, order?: readonly string[]): string[] {
	return (order ?? Object.keys(reviews)).filter((id) => {
		const e = reviews[id];
		return !!e && !isRetired(e) && e.due <= today;
	});
}

const DAY = /^\d{4}-\d{2}-\d{2}$/;

function readEntry(v: unknown): ReviewEntry | null {
	const r = v as Partial<ReviewEntry> | null;
	const ok = !!r && typeof r === 'object' && Number.isInteger(r.box) && (r.box as number) >= 1 && typeof r.due === 'string' && DAY.test(r.due);
	return ok ? { box: Math.min(RETIRED, r.box as number), due: r.due as string } : null;
}

/** Reads a stored schedule, dropping damaged entries. */
export function readReviews(v: unknown): LessonReviews {
	if (!v || typeof v !== 'object' || Array.isArray(v)) return {};
	const out: LessonReviews = {};
	for (const [id, e] of Object.entries(v as Record<string, unknown>)) {
		const r = readEntry(e);
		if (r) out[id] = r;
	}
	return out;
}
