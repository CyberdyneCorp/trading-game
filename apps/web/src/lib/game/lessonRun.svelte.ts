// View-model of one Academy lesson run (design D2): the steps in order (all of them, or only the
// decision and transfer questions in a spaced review), the answer to the current question and the
// first try of each one. A wrong answer explains and leaves the question open; Next unlocks once it
// is answered right. Choice, typed number (either decimal mark, the locale's thousands groups, an
// optional unit) and `set` questions (a widget value within a tolerance of a target) are graded here.

import { isQuestion, reviewSteps, type Lesson, type LessonStep } from '#lib/content/academy.ts';
import { reachesTarget } from '#lib/content/ticketSandbox.ts';
import { parseAmount } from './format.ts';
import type { Locale } from './i18n.svelte.ts';

export type LessonMode = 'lesson' | 'review';
export type AnswerStatus = 'idle' | 'right' | 'wrong';

/** A typed answer as a number: "−1,20", "2.340" (PT-BR thousands), "6,25%" or "2 R"; NaN when invalid. */
export function readTypedNumber(input: string, locale: Locale): number {
	const s = input.replace(/[−–]/g, '-').replace(/\s|%|×|cr|R$/gi, '');
	return parseAmount(s, locale);
}

export class LessonRun {
	readonly lesson: Lesson;
	readonly steps: LessonStep[];
	i = $state(0);
	/** The choice picked last on this step (choice questions). */
	picked = $state<number | null>(null);
	status = $state<AnswerStatus>('idle');
	firstTry = $state(new Map<string, boolean>());
	finished = $state(false);
	private readonly solvedIds = new Set<string>();

	constructor(lesson: Lesson, mode: LessonMode = 'lesson') {
		this.lesson = lesson;
		this.steps = mode === 'review' ? reviewSteps(lesson) : lesson.steps;
	}

	get step(): LessonStep | undefined {
		return this.steps[this.i];
	}

	get solved(): boolean {
		return !this.step?.question || this.status === 'right';
	}

	get last(): boolean {
		return this.i >= this.steps.length - 1;
	}

	get score(): { right: number; total: number } {
		const questions = this.steps.filter(isQuestion);
		return { right: questions.filter((s) => this.firstTry.get(s.id) === true).length, total: questions.length };
	}

	get allRightFirstTry(): boolean {
		const s = this.score;
		return s.right === s.total;
	}

	private grade(right: boolean): void {
		const step = this.step;
		if (!step) return;
		if (!this.firstTry.has(step.id)) this.firstTry = new Map(this.firstTry).set(step.id, right);
		if (right) this.solvedIds.add(step.id);
		this.status = right ? 'right' : 'wrong';
	}

	pickChoice(k: number): void {
		const q = this.step?.question;
		if (!q || q.type === 'number' || q.type === 'set' || this.solved) return;
		this.picked = k;
		this.grade(k === q.answer);
	}

	submitNumber(input: string, locale: Locale): void {
		const q = this.step?.question;
		if (q?.type !== 'number' || this.solved) return;
		const v = readTypedNumber(input, locale);
		if (Number.isNaN(v)) return; // nothing graded until a number is typed
		this.grade(reachesTarget(v, q.answer, q.tolerance));
	}

	/** A `set` question checked against the widget's current value. */
	checkSet(value: number | null): void {
		const q = this.step?.question;
		if (q?.type !== 'set' || this.solved) return;
		this.grade(reachesTarget(value, q.target, q.tolerance));
	}

	next(): void {
		if (!this.solved) return;
		if (this.last) this.finished = true;
		else this.go(this.i + 1);
	}

	prev(): void {
		if (this.i > 0) this.go(this.i - 1);
	}

	private go(i: number): void {
		this.i = i;
		this.picked = null;
		// A step already answered right stays solved when the player comes back to it.
		this.status = this.solvedIds.has(this.steps[i].id) ? 'right' : 'idle';
	}
}
