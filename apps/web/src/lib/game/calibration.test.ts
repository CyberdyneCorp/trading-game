import { describe, expect, it } from 'vitest';
import { stepsUntilNextQuestion } from './calibration.ts';
import { game } from './fixtures.test-util.ts';

describe('stepsUntilNextQuestion', () => {
	const g = game([], { status: 'active', warmupBars: 10, step: 10, totalBars: 60 });
	it('stops on the next question step', () => {
		expect(stepsUntilNextQuestion(g, 5)).toBe(5);
		expect(stepsUntilNextQuestion(g, 50)).toBe(15); // next question at 25
		expect(stepsUntilNextQuestion({ ...g, step: 22 }, 5)).toBe(3);
	});
	it('does not stop when the next question could not resolve', () => {
		expect(stepsUntilNextQuestion({ ...g, step: 41 }, 50)).toBe(50); // 55 + 5 >= 60
	});
});
