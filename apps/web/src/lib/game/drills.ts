// Daily drills (add-practice-and-advanced-strategies design D15): six items in about two minutes on
// payoffs, Greeks, sizing, liquidation prices and the rest, fed by the spaced review. Items are drawn
// from the drill templates (content/drills) with mulberry32 seeded by FNV-1a 32 of
// "drill-v1:" + local date + ":" + session index; decimal parameters are integer multiples of their
// step, never floating-point products. Answers come from the lesson check functions (checks.ts). The
// desktop ports this module with the parity fixture drills.json.

import { computeCheck, type Check } from '#lib/content/checks.ts';
import {
	DRILL_SKILLS,
	templatesOf,
	skillsForLessons,
	type AnswerSpec,
	type DrillSkill,
	type DrillTemplate,
	type ParamSpec,
	type ChoiceSpec
} from '#lib/content/drills.ts';
import { fmtNum } from './format.ts';
import type { Locale } from './i18n.svelte.ts';
import { fnv1a32, mulberry32, pick } from './prng.ts';
import { addDays, dueReviews, REVIEW_DAYS } from './review.ts';
import { DRILL_HISTORY_KEEP, type DrillReview, type DrillSession, type Progress } from './storage.ts';

export const DRILL_ITEMS = 6;
/** The soft timer: shown, never failing (unanswered items count as skipped). */
export const DRILL_SECONDS = 120;
/** At most this many items come from skills of lessons whose review is due. */
export const DRILL_FROM_LESSONS = 3;

export interface DrillItem {
	template: string;
	skill: DrillSkill;
	lessons: string[];
	/** Parameters as canonical decimal strings, in the template's key order. */
	params: Record<string, string>;
	/** The right answer (a choice index for choice items). */
	answer: number;
	digits: number;
	tolerance: number;
	choice: boolean;
}

// ---- decimal parameters on their grid ----

const decimalsOf = (s: string): number => (s.includes('.') ? s.length - s.indexOf('.') - 1 : 0);

/** A decimal string as integer units at `d` decimals ("1.5", 2 → 150). */
function units(s: string, d: number): number {
	const [i, f = ''] = s.split('.');
	return Number(i) * 10 ** d + Number(f.padEnd(d, '0') || '0');
}

/** Integer units at `d` decimals as a canonical decimal string (no trailing zeros). */
function fromUnits(u: number, d: number): string {
	const neg = u < 0;
	const a = Math.abs(u);
	const scale = 10 ** d;
	const int = Math.floor(a / scale);
	const frac =
		d > 0
			? String(a % scale)
					.padStart(d, '0')
					.replace(/0+$/, '')
			: '';
	return `${neg ? '-' : ''}${int}${frac ? `.${frac}` : ''}`;
}

function drawParam(spec: ParamSpec, done: Record<string, string>, next: () => number): string {
	if ('lo' in spec) {
		const d = Math.max(decimalsOf(spec.lo), decimalsOf(spec.hi), decimalsOf(spec.step));
		const lo = units(spec.lo, d);
		const step = units(spec.step, d);
		const count = Math.floor((units(spec.hi, d) - lo) / step) + 1;
		return fromUnits(lo + pick(next, count) * step, d);
	}
	if ('of' in spec) return spec.of[pick(next, spec.of.length)];
	const [a, b] = 'add' in spec ? spec.add : spec.sub;
	const d = Math.max(decimalsOf(done[a]), decimalsOf(done[b]));
	const sign = 'add' in spec ? 1 : -1;
	return fromUnits(units(done[a], d) + sign * units(done[b], d), d);
}

/** Draws a template's parameters in its key order (sums and differences draw nothing). */
export function drawParams(t: DrillTemplate, next: () => number): Record<string, string> {
	const out: Record<string, string> = {};
	for (const [name, spec] of Object.entries(t.params)) out[name] = drawParam(spec, out, next);
	return out;
}

// ---- answers through the lesson checks ----

function resolve(v: unknown, params: Record<string, string>): unknown {
	if (typeof v === 'string' && v.startsWith('$')) return Number(params[v.slice(1)]);
	if (Array.isArray(v)) return v.map((x) => resolve(x, params));
	if (v && typeof v === 'object') return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, resolve(x, params)]));
	return v;
}

/** The check an item's answer is computed by (choice items: none). */
export function itemCheck(t: DrillTemplate, params: Record<string, string>): Check | null {
	if (t.answer.kind === 'choice') return null;
	const { index: _index, ...spec } = t.answer as AnswerSpec;
	return { ...(resolve(spec, params) as Check), digits: t.digits ?? 2 };
}

/** The right answer for these parameters; null when the check cannot compute it. */
export function answerOf(t: DrillTemplate, params: Record<string, string>): number | null {
	if (t.answer.kind === 'choice') {
		const c = t.answer as ChoiceSpec;
		return c.answers[Number(params[c.from])] ?? null;
	}
	const v = computeCheck(itemCheck(t, params) as Check);
	const index = (t.answer as { index?: number }).index ?? 0;
	return Array.isArray(v) ? (v[index] ?? null) : v;
}

export function makeItem(t: DrillTemplate, next: () => number): DrillItem {
	const params = drawParams(t, next);
	const digits = t.digits ?? 2;
	return {
		template: t.id,
		skill: t.skill,
		lessons: [...t.lessons],
		params,
		answer: answerOf(t, params) ?? NaN,
		digits,
		tolerance: t.tolerance ?? 0.5 * 10 ** -digits,
		choice: t.answer.kind === 'choice'
	};
}

/** An answer as typed: a number with a comma or a dot; NaN otherwise. */
export function parseAnswer(text: string): number {
	const s = text.trim().replace(/\s/g, '').replace(',', '.');
	return /^-?\d+(\.\d+)?$/.test(s) ? Number(s) : NaN;
}

/** Is the typed (or chosen) answer right. */
export function isRight(item: DrillItem, given: number): boolean {
	if (item.choice) return given === item.answer;
	return Number.isFinite(given) && Math.abs(given - item.answer) <= item.tolerance + 1e-9;
}

// ---- texts ----

function fill(text: string, values: Record<string, string>): string {
	return text.replace(/\{(\w+)\}/g, (m, k: string) => values[k] ?? m);
}

/** The prompt, explanation and choices of an item in a locale, its numbers formatted for it. */
export function itemText(item: DrillItem, t: DrillTemplate, locale: Locale): { prompt: string; explain: string; choices: string[]; answer: string } {
	const x = t.text[locale];
	const values: Record<string, string> = {};
	for (const [k, v] of Object.entries(item.params)) values[k] = fmtNum(Number(v), locale, decimalsOf(v));
	if (x.cases && t.answer.kind === 'choice') values[(t.answer as ChoiceSpec).from] = x.cases[Number(item.params[(t.answer as ChoiceSpec).from])];
	values.answer = item.choice ? (x.choices?.[item.answer] ?? '') : fmtNum(item.answer, locale, item.digits);
	return {
		prompt: fill(x.prompt, values),
		explain: fill(x.explain, values),
		choices: x.choices ?? [],
		answer: values.answer
	};
}

// ---- which skills a session asks ----

export interface SkillStats {
	n: number;
	right: number;
}

/** First-try results per skill over the kept sessions. */
export function skillStats(history: readonly DrillSession[]): Record<string, SkillStats> {
	const out: Record<string, SkillStats> = {};
	for (const s of history) {
		for (const [k, v] of Object.entries(s.skills ?? {})) {
			const o = (out[k] ??= { n: 0, right: 0 });
			o.n += v.n;
			o.right += v.right;
		}
	}
	return out;
}

/** The seed of a session: FNV-1a 32 of "drill-v1:" + date + ":" + session index (0 = the day's first). */
export const drillSeed = (date: string, session: number): number => fnv1a32(`drill-v1:${date}:${session}`);

/** The day's next session index: the sessions already kept for that date. */
export const sessionIndex = (history: readonly DrillSession[], date: string): number => history.filter((s) => s.date === date).length;

export interface SessionInput {
	date: string;
	now: number;
	session: number;
	lessonReviews: Progress['lessonReviews'];
	drillReviews: Record<string, DrillReview>;
	history: readonly DrillSession[];
}

function pushUnique(out: DrillSkill[], skills: readonly DrillSkill[], limit: number): void {
	for (const s of skills) if (out.length < limit && !out.includes(s)) out.push(s);
}

/**
 * The skills of a session, in order: up to 3 tied to lessons whose review is due, then skills due in the
 * drill schedule (earliest first), then the lowest first-try rate (unseen counts 50%), ties in a drawn
 * order. `rank` is that draw (one output per skill).
 */
export function chooseSkills(input: SessionInput, rank: readonly number[]): DrillSkill[] {
	const out: DrillSkill[] = [];
	pushUnique(out, skillsForLessons(dueReviews(input.lessonReviews, input.date)), DRILL_FROM_LESSONS);
	const due = DRILL_SKILLS.filter((s) => input.drillReviews[s] && input.drillReviews[s].due <= input.now).sort(
		(a, b) => input.drillReviews[a].due - input.drillReviews[b].due
	);
	pushUnique(out, due, DRILL_ITEMS);
	const stats = skillStats(input.history);
	const rate = (s: DrillSkill) => (stats[s]?.n ? stats[s].right / stats[s].n : 0.5);
	const order = DRILL_SKILLS.map((s, i) => ({ s, i })).sort((a, b) => rate(a.s) - rate(b.s) || rank[a.i] - rank[b.i]);
	pushUnique(
		out,
		order.map((x) => x.s),
		DRILL_ITEMS
	);
	return out;
}

/** A session's items: the skills' order drawn first (one output per skill), then each item's template and parameters. */
export function buildSession(input: SessionInput): DrillItem[] {
	const next = mulberry32(drillSeed(input.date, input.session));
	const rank = DRILL_SKILLS.map(() => next());
	return chooseSkills(input, rank).map((skill) => {
		const ts = templatesOf(skill);
		return makeItem(ts[pick(next, ts.length)], next);
	});
}

// ---- after a session ----

export type ItemResult = 'right' | 'wrong' | 'skipped';

/** Midnight (local) of a day, as epoch ms. */
function dayStart(day: string): number {
	const [y, m, d] = day.split('-').map(Number);
	return new Date(y, m - 1, d).getTime();
}

/**
 * The drill schedule after a session: a skill whose items were all right on the first try moves up a
 * box (1, 3, 7, 21 days; an unseen skill counts as box 1); any wrong first answer sends it to box 1;
 * skipped items change nothing.
 */
export function scheduleSkills(
	reviews: Record<string, DrillReview>,
	items: readonly DrillItem[],
	results: readonly ItemResult[],
	date: string
): Record<string, DrillReview> {
	const out = { ...reviews };
	for (const skill of new Set(items.map((i) => i.skill))) {
		const mine = results.filter((_, k) => items[k].skill === skill);
		if (mine.every((r) => r === 'skipped')) continue;
		const box = mine.includes('wrong') ? 1 : Math.min(REVIEW_DAYS.length, (reviews[skill]?.box ?? 1) + 1);
		out[skill] = { box, due: dayStart(addDays(date, REVIEW_DAYS[box - 1])) };
	}
	return out;
}

/** The session kept in the history (the newest DRILL_HISTORY_KEEP). */
export function sessionRecord(items: readonly DrillItem[], results: readonly ItemResult[], date: string): DrillSession {
	const skills: Record<string, SkillStats> = {};
	items.forEach((it, k) => {
		if (results[k] === 'skipped') return;
		const s = (skills[it.skill] ??= { n: 0, right: 0 });
		s.n += 1;
		s.right += results[k] === 'right' ? 1 : 0;
	});
	return {
		date,
		items: items.length,
		firstTry: results.filter((r) => r === 'right').length,
		skills
	};
}

/** Progress after a finished session (drill answers never complete a lesson review). */
export function finishSession(p: Progress, items: readonly DrillItem[], results: readonly ItemResult[], date: string): Progress {
	return {
		...p,
		drillReviews: scheduleSkills(p.drillReviews, items, results, date),
		drillHistory: [...p.drillHistory, sessionRecord(items, results, date)].slice(-DRILL_HISTORY_KEEP)
	};
}
