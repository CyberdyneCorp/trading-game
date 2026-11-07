// Academy lessons as data (design D1, D2 of add-academy-and-strategies): one JSON file per lesson in
// ./academy/, PT-BR and en side by side per step, plus ./academy/index.json with the lesson order,
// stages, titles and summaries. The index is imported eagerly (the map and the Academy screen list
// every lesson); each lesson file is imported lazily when it opens, so lessons stay out of the
// initial bundle. The desktop generator reads the same files (apps/desktop/scripts).
//
// Lessons are client data, unlike a mission's lesson questions (lessons.ts, scored by the engine).
// They never read a run: every chart is a small illustrative window and every stated number is
// declared as a `check` (content/checks.ts verifies them in the unit tests).

import type { StageId } from '#lib/game/curriculum.ts';
import type { Locale } from '#lib/game/i18n.svelte.ts';
import type { Check } from './checks.ts';
import indexJson from './academy/index.json';

export const LOCALES: readonly Locale[] = ['pt-BR', 'en'];

export type Localized<T> = Record<Locale, T>;

export interface Candle {
	o: number;
	h: number;
	l: number;
	c: number;
	v: number;
}

/** Horizontal lines a lesson chart can label (design D2: entry, stop, target, liquidation, mark and last, strike). */
export type LevelKind = 'entry' | 'stop' | 'target' | 'liquidation' | 'mark' | 'last' | 'strike';
export const LEVEL_KINDS: readonly LevelKind[] = ['entry', 'stop', 'target', 'liquidation', 'mark', 'last', 'strike'];

export interface Level {
	kind: LevelKind;
	price: number;
}

export interface LessonChart {
	/** Illustrative market id; the transfer exercise uses one the demonstration did not. */
	market: string;
	interval: string;
	candles: Candle[];
	/** 1-based bar to highlight. */
	highlight?: number;
	/** Bars already closed; the rest is the unknown future (hatched, nothing drawn). */
	known?: number;
	/** Draw one large annotated candle (open, high, low, close, body, wicks). */
	anatomy?: boolean;
	levels?: Level[];
}

/** Ticket sandbox fields the player may change (the others are fixed by the step). */
export type TicketField = 'side' | 'qty' | 'leverage' | 'stop' | 'target';
export const TICKET_FIELDS: readonly TicketField[] = ['side', 'qty', 'leverage', 'stop', 'target'];
/** Ticket sandbox outputs (content/ticketSandbox.ts). */
export type TicketOutput = 'notional' | 'margin' | 'liquidation' | 'riskAtStop' | 'riskPct' | 'rTarget';
export const TICKET_OUTPUTS: readonly TicketOutput[] = ['notional', 'margin', 'liquidation', 'riskAtStop', 'riskPct', 'rTarget'];

export interface TicketWidget {
	kind: 'ticket';
	market: string;
	equity: number;
	entry: number;
	side: 'buy' | 'sell';
	qty: number;
	leverage: number;
	maxLeverage: number;
	stop: number | null;
	target: number | null;
	/** Maintenance margin rate of the sandbox contract. */
	mmr: number;
	lot: number;
	edit: TicketField[];
}

export interface TableWidget {
	kind: 'table';
	/** Illustrative market (or journal) the rows belong to. */
	market: string;
	/** Column ids; their headers are the step text's `columns`. */
	columns: { id: string; digits?: number; suffix?: string }[];
	/** Numbers are written in the locale; strings are ids of the step text's `cells`. */
	rows: (number | string)[][];
}

export interface PayoffLeg {
	kind: 'call' | 'put' | 'perp';
	side: 'buy' | 'sell';
	qty: number;
	/** Options only. */
	strike?: number;
	/** Premium per unit (options) or entry price (perpetual). */
	price: number;
}

export interface PayoffWidget {
	kind: 'payoff';
	market: string;
	legs: PayoffLeg[];
	lo: number;
	hi: number;
	/** Hide the breakevens and the maximum profit and loss (a question asks for them). */
	hideAnalytics?: boolean;
}

export type LessonWidget = TicketWidget | TableWidget | PayoffWidget;
export type WidgetKind = LessonWidget['kind'];

export type Choice = { kind: 'bar'; bar: number } | { kind: 'pct'; value: number } | { kind: 'text'; id: string };

export interface ChoiceQuestion {
	type?: 'choice';
	choices: Choice[];
	answer: number;
}

export interface NumberQuestion {
	type: 'number';
	answer: number;
	/** Absolute tolerance; the typed answer accepts either decimal mark. */
	tolerance: number;
	/** Shown after the input: '%', 'cr', '×', 'R' or a price (no unit). */
	unit?: string;
}

/** Reached by moving the step's ticket sandbox until `field` is within `tolerance` of `target`. */
export interface SetQuestion {
	type: 'set';
	field: TicketField | TicketOutput;
	target: number;
	tolerance: number;
}

export type Question = ChoiceQuestion | NumberQuestion | SetQuestion;

export type LessonPhase = 'concept' | 'demo' | 'decision' | 'transfer';
export const PHASES: readonly LessonPhase[] = ['concept', 'demo', 'decision', 'transfer'];

export interface StepText {
	title: string;
	body: string;
	/** Feedback after a right / a wrong answer (questions only). */
	right?: string;
	wrong?: string;
	/** Texts of `text` choices, by choice id. */
	choices?: Record<string, string>;
	/** Feedback for a specific wrong choice, by `text` choice id: the misconception behind it. */
	feedback?: Record<string, string>;
	/** Table widget: column headers, in column order. */
	columns?: string[];
	/** Table widget: texts of string cells, by id. */
	cells?: Record<string, string>;
}

export interface LessonStep {
	id: string;
	phase: LessonPhase;
	chart?: LessonChart;
	widget?: LessonWidget;
	question?: Question;
	text: Localized<StepText>;
}

export interface Lesson {
	id: string;
	stage: StageId;
	steps: LessonStep[];
	/** Every number the texts state, checked by content/checks.ts. */
	checks?: Check[];
	/** Numbers the texts give rather than compute (a convention such as RSI 70, a stated confidence). */
	givens?: number[];
}

export interface LessonMeta {
	id: string;
	stage: StageId;
	title: Localized<string>;
	summary: Localized<string>;
}

/** What the runner shows for one locale (the shape the runner and the desktop generator read). */
export interface LessonText {
	title: string;
	summary: string;
	steps: Record<string, StepText>;
}

/** Every lesson, in Academy order (eager: titles for the map and the Academy screen). */
export const LESSON_INDEX: readonly LessonMeta[] = (indexJson as { lessons: LessonMeta[] }).lessons;

export function lessonMeta(id: string): LessonMeta | null {
	return LESSON_INDEX.find((l) => l.id === id) ?? null;
}

/** Lessons of a curriculum stage, in order. */
export function lessonsOf(stage: StageId): LessonMeta[] {
	return LESSON_INDEX.filter((l) => l.stage === stage);
}

/** A lesson file as written: steps may name a chart of the lesson's `charts` (`ref`) and override fields. */
export interface LessonFile extends Omit<Lesson, 'steps'> {
	charts?: Record<string, LessonChart>;
	steps: (Omit<LessonStep, 'chart'> & { chart?: Partial<LessonChart> & { ref?: string } })[];
}

/** Resolves chart references (`{"ref": "S", "highlight": 9}`) into full charts. */
export function normalizeLesson(file: LessonFile): Lesson {
	const steps = file.steps.map((s) => {
		if (!s.chart) return s as LessonStep;
		const { ref, ...own } = s.chart;
		const base = ref ? file.charts?.[ref] : undefined;
		return { ...s, chart: { ...base, ...own } as LessonChart };
	});
	return { id: file.id, stage: file.stage, steps, ...(file.checks ? { checks: file.checks } : {}), ...(file.givens ? { givens: file.givens } : {}) };
}

const files = import.meta.glob<{ default: unknown }>(['./academy/*.json', '!./academy/index.json']);
const loaded = new Map<string, Lesson>();

/** Loads and validates one lesson file (null when it does not exist or is invalid). */
export async function loadLesson(id: string): Promise<Lesson | null> {
	const cached = loaded.get(id);
	if (cached) return cached;
	const load = files[`./academy/${id}.json`];
	if (!load || !lessonMeta(id)) return null;
	const lesson = normalizeLesson((await load()).default as LessonFile);
	const problems = lessonProblems(lesson);
	if (problems.length) {
		console.error(`lesson ${id}: ${problems.join('; ')}`);
		return null;
	}
	loaded.set(id, lesson);
	return lesson;
}

/** Every lesson of the index, loaded (tests and the desktop generator's checks). */
export async function loadAllLessons(): Promise<Lesson[]> {
	const all = await Promise.all(LESSON_INDEX.map((m) => loadLesson(m.id)));
	return all.filter((l): l is Lesson => l !== null);
}

export function lessonText(lesson: Lesson, locale: Locale): LessonText {
	const meta = lessonMeta(lesson.id);
	return {
		title: meta?.title[locale] ?? lesson.id,
		summary: meta?.summary[locale] ?? '',
		steps: Object.fromEntries(lesson.steps.map((s) => [s.id, s.text[locale]]))
	};
}

export const isQuestion = (s: LessonStep) => s.question !== undefined;

/** Questions answered right on the first try, out of all questions. */
export function firstTryScore(lesson: Pick<Lesson, 'steps'>, firstTry: ReadonlyMap<string, boolean>): { right: number; total: number } {
	const questions = lesson.steps.filter(isQuestion);
	return { right: questions.filter((s) => firstTry.get(s.id) === true).length, total: questions.length };
}

/** The steps a spaced review asks: the decision and transfer questions, in order (design D9). */
export function reviewSteps(lesson: Lesson): LessonStep[] {
	return lesson.steps.filter((s) => isQuestion(s) && (s.phase === 'decision' || s.phase === 'transfer'));
}

// ---- schema validation (the loader refuses an invalid file; the unit tests list every problem) ----

/** The `text` choice ids of a choice question (none for number and set questions). */
function textChoiceIds(q: Question | undefined): string[] {
	if (!q || q.type === 'number' || q.type === 'set') return [];
	return q.choices.flatMap((c) => (c.kind === 'text' ? [c.id] : []));
}

function localeTextProblems(s: LessonStep, locale: Locale): string[] {
	const tx = s.text?.[locale];
	const at = `${s.id}: ${locale}`;
	if (!tx?.title || !tx.body) return [`${at} title/body missing`];
	const out: string[] = [];
	if (s.question && !(tx.right && tx.wrong)) out.push(`${at} right/wrong feedback missing`);
	const ids = textChoiceIds(s.question);
	for (const id of ids) if (!tx.choices?.[id]) out.push(`${at} choice ${id} has no text`);
	for (const id of Object.keys(tx.feedback ?? {})) if (!ids.includes(id)) out.push(`${at} feedback for unknown choice ${id}`);
	if (s.widget?.kind === 'table' && tx.columns?.length !== s.widget.columns.length) out.push(`${at} table headers`);
	return out;
}

function textProblems(s: LessonStep): string[] {
	return LOCALES.flatMap((locale) => localeTextProblems(s, locale));
}

const isTicketKey = (f: string) => (TICKET_FIELDS as readonly string[]).includes(f) || (TICKET_OUTPUTS as readonly string[]).includes(f);

function setQuestionOk(q: SetQuestion, w: LessonWidget | undefined): boolean {
	return w?.kind === 'ticket' && isTicketKey(q.field) && w.edit.length > 0;
}

function choiceQuestionOk(q: ChoiceQuestion): boolean {
	return q.choices.length >= 2 && Number.isInteger(q.answer) && q.answer >= 0 && q.answer < q.choices.length;
}

function questionOk(q: Question, w: LessonWidget | undefined): boolean {
	if (q.type === 'number') return Number.isFinite(q.answer) && q.tolerance >= 0;
	if (q.type === 'set') return setQuestionOk(q, w);
	return choiceQuestionOk(q);
}

function questionProblems(s: LessonStep): string[] {
	const q = s.question;
	if (!q) return [];
	if (s.phase === 'concept' || s.phase === 'demo') return [`${s.id}: a question in a ${s.phase} step`];
	return questionOk(q, s.widget) ? [] : [`${s.id}: bad ${q.type ?? 'choice'} question`];
}

const ticketOk = (w: TicketWidget) =>
	w.entry > 0 && w.equity > 0 && w.lot > 0 && w.leverage >= 1 && w.leverage <= w.maxLeverage && w.edit.every((f) => TICKET_FIELDS.includes(f));
const tableOk = (w: TableWidget) => w.rows.every((r) => r.length === w.columns.length);
const payoffOk = (w: PayoffWidget) =>
	w.legs.length >= 1 &&
	w.legs.length <= 5 &&
	w.legs.filter((l) => l.kind === 'perp').length <= 1 &&
	w.legs.every((l) => l.kind === 'perp' || (l.strike ?? 0) > 0) &&
	w.lo < w.hi;

function widgetOk(w: LessonWidget): boolean {
	if (w.kind === 'ticket') return ticketOk(w);
	if (w.kind === 'table') return tableOk(w);
	if (w.kind === 'payoff') return payoffOk(w);
	return false;
}

function widgetProblems(s: LessonStep): string[] {
	return !s.widget || widgetOk(s.widget) ? [] : [`${s.id}: bad ${s.widget.kind} widget`];
}

function chartProblems(s: LessonStep): string[] {
	const c = s.chart;
	if (!c) return [];
	const out: string[] = [];
	if (!c.market || !c.interval || c.candles.length === 0) out.push(`${s.id}: chart needs market, interval and bars`);
	for (const k of c.candles) if (k.h < Math.max(k.o, k.c) || k.l > Math.min(k.o, k.c)) out.push(`${s.id}: inconsistent bar`);
	for (const l of c.levels ?? []) if (!LEVEL_KINDS.includes(l.kind)) out.push(`${s.id}: unknown level ${l.kind}`);
	return out;
}

/** Schema problems of one lesson file; empty = valid. */
export function lessonProblems(lesson: Lesson): string[] {
	if (!lesson?.id || !Array.isArray(lesson.steps) || lesson.steps.length === 0) return ['lesson needs an id and steps'];
	const out: string[] = [];
	const ids = new Set<string>();
	for (const s of lesson.steps) {
		if (ids.has(s.id)) out.push(`duplicate step ${s.id}`);
		ids.add(s.id);
		if (!PHASES.includes(s.phase)) out.push(`${s.id}: unknown phase`);
		out.push(...textProblems(s), ...questionProblems(s), ...widgetProblems(s), ...chartProblems(s));
	}
	const phases = lesson.steps.map((s) => PHASES.indexOf(s.phase));
	if (phases.some((p, i) => i > 0 && p < phases[i - 1])) out.push('phases out of order');
	return out;
}
