import { describe, expect, it } from 'vitest';
import { loadLesson } from '#lib/content/academy.ts';
import { initialInputs, sandboxOutputs, sandboxValue } from '#lib/content/ticketSandbox.ts';
import { LessonRun, readTypedNumber } from './lessonRun.svelte.ts';

const futures = (await loadLesson('futures-pnl'))!;
const sizing = (await loadLesson('risk-sizing'))!;
const payoff = (await loadLesson('options-payoff'))!;

const goTo = (run: LessonRun, id: string) => {
	while (run.step?.id !== id) {
		if (!run.solved) throw new Error(`stuck at ${run.step?.id}`);
		run.next();
	}
};

describe('lesson run view-model', () => {
	it('reads typed numbers with either decimal mark, the locale thousands and a unit', () => {
		expect(readTypedNumber('−1,20', 'pt-BR')).toBe(-1.2);
		expect(readTypedNumber('-1.20', 'pt-BR')).toBe(-1.2);
		expect(readTypedNumber('2.340', 'pt-BR')).toBe(2340);
		expect(readTypedNumber('2,340', 'en')).toBe(2340);
		expect(readTypedNumber('6,25%', 'pt-BR')).toBe(6.25);
		expect(readTypedNumber('2 R', 'en')).toBe(2);
		expect(readTypedNumber('abc', 'en')).toBeNaN();
	});

	it('a typed number: wrong keeps the question open and counts as the first try; right unlocks Next', () => {
		const run = new LessonRun(futures);
		goTo(run, 'short');
		expect(run.solved).toBe(false);
		run.next();
		expect(run.step?.id).toBe('short'); // Next waits for the answer
		run.submitNumber('-18', 'pt-BR');
		expect(run.status).toBe('wrong');
		run.submitNumber('18', 'pt-BR');
		expect(run.status).toBe('right');
		expect(run.firstTry.get('short')).toBe(false);
		run.next();
		run.prev();
		expect(run.solved).toBe(true); // an answered step stays answered
	});

	it('Wrong answer explained: a distractor is graded wrong and the question stays open', () => {
		const run = new LessonRun(payoff);
		goTo(run, 'be');
		run.pickChoice(1); // 100: the strike
		expect(run.status).toBe('wrong');
		expect(run.picked).toBe(1);
		run.pickChoice(0);
		expect(run.status).toBe('right');
		run.pickChoice(2); // ignored once solved
		expect(run.picked).toBe(0);
	});

	it('Sizing by moving the ticket: the set question is right once the risk at the stop is 1% of 10,000', () => {
		const run = new LessonRun(sizing);
		goTo(run, 'set-size');
		const w = run.step!.widget!;
		if (w.kind !== 'ticket') throw new Error('ticket expected');
		const value = (qty: number) => {
			const x = { ...initialInputs(w), qty };
			return sandboxValue('riskAtStop', x, sandboxOutputs(w, x));
		};
		run.checkSet(value(10));
		expect(run.status).toBe('wrong');
		run.checkSet(value(25));
		expect(run.status).toBe('right');
		expect(value(25)).toBe(100);
	});

	it('a review asks only the decision and transfer questions and reports all right on the first try', () => {
		const run = new LessonRun(futures, 'review');
		expect(run.steps.map((s) => s.id)).toEqual(['short', 'fees', 'level']);
		run.submitNumber('18', 'en');
		run.next();
		run.pickChoice(0);
		run.next();
		run.submitNumber('-30', 'en');
		run.next();
		expect(run.finished).toBe(true);
		expect(run.score).toEqual({ right: 3, total: 3 });
		expect(run.allRightFirstTry).toBe(true);
	});
});
