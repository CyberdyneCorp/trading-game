import { describe, expect, it } from 'vitest';
import { calibrationScore, nextQuestionStep, outcome, questionDue } from './calibration.ts';
import { bars, game } from './fixtures.test-util.ts';

describe('calibration', () => {
	const g = game([], { status: 'active', warmupBars: 10, step: 10, totalBars: 60 });

	it('schedules questions every 15 bars while they can still resolve', () => {
		expect(nextQuestionStep(g, [])).toBe(10);
		expect(questionDue(g, [])).toBe(true);
		const asked = [{ step: 10, threshold: '100', horizon: 5, p: 0.5 }];
		expect(nextQuestionStep(g, asked)).toBe(25);
		expect(nextQuestionStep({ ...g, step: 50 }, asked)).toBeNull(); // 55 > 59 - 5... last resolvable is 54
	});

	it('resolves only once the horizon bar is visible (never from the future)', () => {
		const q = { step: 2, threshold: '100', horizon: 5, p: 0.7 };
		expect(outcome(q, bars([100, 100, 100, 100, 100]))).toBeNull();
		expect(outcome(q, bars([100, 100, 100, 100, 100, 100, 100, 101]))).toBe(1);
	});

	it('needs at least three resolved answers and rewards calibrated confidence', () => {
		const b = bars([100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110]);
		const qs = [0, 1, 2].map((s) => ({ step: s, threshold: '100', horizon: 5, p: 0.9 }));
		expect(calibrationScore(qs.slice(0, 2), b).score).toBeNull();
		expect(calibrationScore(qs, b).score).toBe(99); // (0.1)^2 = 0.01
		const unsure = qs.map((q) => ({ ...q, p: 0.5 }));
		expect(calibrationScore(unsure, b).score).toBe(75);
		const wrong = qs.map((q) => ({ ...q, p: 0.1 }));
		expect(calibrationScore(wrong, b).score).toBe(19);
		const skipped = qs.map((q) => ({ ...q, p: null }));
		expect(calibrationScore(skipped, b).resolved).toBe(0);
	});
});
