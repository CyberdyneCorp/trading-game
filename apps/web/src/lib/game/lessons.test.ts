import RUBRIC_TEXT from '../../../../../engine/rubrics/lessons.json?raw';
import { describe, expect, it } from 'vitest';
import { answerText, lessonMissions, numericAnswer, questionText, showableQuestions } from './lessons.ts';
import type { LessonInfo, LessonResultView } from '#lib/engine/types.ts';

// The engine's rubric file: the structure and the key; the texts here must match it.
interface RubricFile {
	rubrics: { mission: string; reviewed: boolean; questions: { id: string; kind: 'choice' | 'numeric'; choices?: number }[] }[];
}
const RUBRICS = JSON.parse(RUBRIC_TEXT) as RubricFile;

describe('lesson questions', () => {
	it('every rubric question has PT-BR and English text with the engine number of options', () => {
		expect(lessonMissions().sort()).toEqual(RUBRICS.rubrics.map((r) => r.mission).sort());
		for (const r of RUBRICS.rubrics) {
			for (const q of r.questions) {
				for (const locale of ['pt-BR', 'en'] as const) {
					const text = questionText(r.mission, q.id, locale);
					expect(text, `${r.mission}/${q.id}/${locale}`).not.toBeNull();
					expect(text!.choices.length).toBe(q.kind === 'numeric' ? 0 : q.choices);
				}
			}
		}
	});

	it('PT-BR numbers in the prompts use the decimal comma', () => {
		for (const r of RUBRICS.rubrics) {
			for (const q of r.questions) expect(questionText(r.mission, q.id, 'pt-BR')!.prompt).not.toMatch(/\d\.\d{1,2}(?!\d)/); // "10.000" is a PT-BR thousands separator, "2.50" would be wrong
		}
	});

	it('accepts a decimal comma or point and refuses what is not a non-negative number', () => {
		expect(numericAnswer('90,45')).toBe('90.45000000');
		expect(numericAnswer('107.5')).toBe('107.50000000');
		expect(numericAnswer('1.000')).toBe('1.00000000');
		expect(numericAnswer('abc')).toBeNull();
		expect(numericAnswer('-3')).toBeNull();
		expect(numericAnswer('')).toBeNull();
	});

	it('shows only questions whose text matches the engine (a newer engine question is left out)', () => {
		const info: LessonInfo = {
			mission: 'm01-false-breakout',
			reviewed: false,
			questions: [
				{ id: 'breakout-plan', kind: 'choice', choices: 3, points: 1 },
				{ id: 'new-question', kind: 'choice', choices: 2, points: 1 },
				{ id: 'risk-two-percent', kind: 'numeric', choices: 0, points: 1 }
			]
		};
		expect(showableQuestions(info, 'pt-BR').map((q) => q.index)).toEqual([0, 2]);
	});

	it('writes answers in the locale', () => {
		const numeric: LessonResultView = {
			id: 'liquidation-price', kind: 'numeric', points: 0, maxPoints: 1, answered: true, skipped: false, correct: false,
			given: '89.00000000', answer: '90.45000000'
		};
		expect(answerText('m03-volatility-shock', numeric, 'answer', 'pt-BR')).toBe('90,45');
		expect(answerText('m03-volatility-shock', numeric, 'given', 'pt-BR')).toBe('89');
		const choice: LessonResultView = { ...numeric, id: 'leverage-for-stop', kind: 'choice', given: 0, answer: 1 };
		expect(answerText('m03-volatility-shock', choice, 'answer', 'en')).toBe('10×');
		expect(answerText('m03-volatility-shock', { ...choice, given: null }, 'given', 'en')).toBe('—');
	});
});
