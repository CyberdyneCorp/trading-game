import { describe, expect, it } from 'vitest';
import { STAGES } from '#lib/game/curriculum.ts';
import type { Locale } from '#lib/game/i18n.svelte.ts';
import {
	firstTryScore,
	LESSON_INDEX,
	lessonProblems,
	lessonsOf,
	lessonText,
	loadAllLessons,
	loadLesson,
	LOCALES,
	reviewSteps,
	type Candle,
	type Choice,
	type Lesson,
	type LessonStep,
	type StepText
} from './academy.ts';
import { checkProblem, numbersIn, type Check } from './checks.ts';
import { linkedTerms, plainText, termById } from './glossary.ts';

const lessons = await loadAllLessons();
const byId = (id: string) => lessons.find((l) => l.id === id)!;
const RAW = import.meta.glob<string>('./academy/*.json', { query: '?raw', import: 'default', eager: true });
const rawFile = (id: string) => RAW[`./academy/${id}.json`];

const textsOf = (tx: StepText): string[] => [
	tx.title,
	tx.body,
	tx.right ?? '',
	tx.wrong ?? '',
	...Object.values(tx.choices ?? {}),
	...Object.values(tx.feedback ?? {}),
	...(tx.columns ?? []),
	...Object.values(tx.cells ?? {})
];
const allTexts = (l: Lesson, locale: Locale) => l.steps.flatMap((s) => textsOf(s.text[locale]));

describe('Academy content (design D1, D2)', () => {
	it('every lesson of the index has a valid file; every file is in the index', async () => {
		expect(lessons.map((l) => l.id)).toEqual(LESSON_INDEX.map((m) => m.id));
		const files = Object.keys(RAW)
			.map((f) => f.replace('./academy/', '').replace('.json', ''))
			.filter((f) => f !== 'index');
		expect(files.sort()).toEqual(LESSON_INDEX.map((m) => m.id).sort());
		for (const l of lessons) expect(lessonProblems(l), l.id).toEqual([]);
		expect(await loadLesson('no-such-lesson')).toBeNull();
	});

	it('A lesson for every stage: each stage lists its index lessons, in order, and every lesson names its stage', () => {
		for (const s of STAGES) {
			expect(s.lessons.length, s.id).toBeGreaterThan(0);
			expect(lessonsOf(s.id).map((m) => m.id)).toEqual(s.lessons);
		}
		for (const m of LESSON_INDEX) expect(byId(m.id).stage).toBe(m.stage);
		expect(LESSON_INDEX).toHaveLength(16);
	});

	it('has PT-BR and en texts for every lesson title, summary and step', () => {
		for (const m of LESSON_INDEX) for (const locale of LOCALES) expect(m.title[locale] && m.summary[locale], `${m.id} ${locale}`).toBeTruthy();
		for (const l of lessons) for (const locale of LOCALES) expect(lessonText(l, locale).steps).toBeDefined();
	});

	it('follows concept, demonstration, decision and transfer; questions only in decision and transfer steps', () => {
		for (const l of lessons) {
			const phases = new Set(l.steps.map((s) => s.phase));
			for (const p of ['concept', 'demo', 'decision', 'transfer']) expect(phases.has(p as never), `${l.id} ${p}`).toBe(true);
			expect(l.steps.filter((s) => s.phase === 'transfer').every((s) => s.question), l.id).toBe(true);
		}
	});

	it('Transfer on another market: every transfer step shows a market or price level its other steps did not use', () => {
		const marketOf = (s: LessonStep) => s.chart?.market ?? s.widget?.market ?? null;
		for (const l of lessons) {
			const shown = new Set(l.steps.filter((s) => s.phase !== 'transfer').map(marketOf));
			for (const t of l.steps.filter((s) => s.phase === 'transfer')) {
				expect(marketOf(t), `${l.id} ${t.id}`).not.toBeNull();
				expect(shown.has(marketOf(t)), `${l.id} ${t.id}`).toBe(false);
			}
		}
	});

	it('Mission named in a lesson: no lesson file contains a mission id', () => {
		for (const m of LESSON_INDEX) expect(rawFile(m.id)).not.toMatch(/\bm\d{2}\b|\bm\d{2}-[a-z]/);
		expect('a lesson about m03-volatility-shock').toMatch(/\bm\d{2}\b|\bm\d{2}-[a-z]/);
	});

	it('every check states the number it computes', () => {
		for (const l of lessons) for (const c of l.checks ?? []) expect(checkProblem(c), l.id).toBeNull();
	});

	it('Wrong breakeven caught: a call with strike 100 bought for 4 does not break even at 105', () => {
		const wrong: Check = { kind: 'breakeven', legs: [{ kind: 'call', side: 'buy', qty: 1, strike: 100, price: 4 }], expect: [105] };
		expect(checkProblem(wrong)).toMatch(/computed \[104\]/);
	});

	it('every number a text states comes from a check, a widget, a chart, an answer or a stated given', () => {
		const unchecked = lessons.flatMap(uncheckedNumbers);
		expect(unchecked).toEqual([]);
	});

	it('a number question answer and a set target appear in a check', () => {
		for (const l of lessons) {
			const stated = new Set(numbersIn(l.checks ?? []).map(norm));
			for (const s of l.steps) {
				const q = s.question;
				if (q?.type === 'number') expect(stated.has(norm(q.answer)), `${l.id} ${s.id}`).toBe(true);
				if (q?.type === 'set') expect(stated.has(norm(q.target)), `${l.id} ${s.id}`).toBe(true);
			}
		}
	});

	it('every linked glossary term exists in both locales', () => {
		for (const l of lessons)
			for (const locale of LOCALES)
				for (const text of allTexts(l, locale))
					for (const id of linkedTerms(text)) expect(termById(id)?.text[locale]?.short, `${l.id}: ${id}`).toBeTruthy();
	});

	it('PT-BR texts write decimals with a comma, en texts with a point', () => {
		for (const l of lessons) {
			expect(allTexts(l, 'pt-BR').join(' '), l.id).not.toMatch(/\d\.\d{1,2}(?!\d)/);
			expect(allTexts(l, 'en').join(' '), l.id).not.toMatch(/\d,\d{1,2}(?!\d)/);
		}
		expect(JSON.stringify(lessonText(byId('foundations-bar'), 'pt-BR'))).toContain('100,80');
	});

	// Playtest: the feedback line already starts with "Right." / "Correto."; a text that opened with
	// "Right:" or "Certo:" read "Right. Right: …".
	it('feedback texts never repeat the right/wrong label the runner puts before them', () => {
		const label = { 'pt-BR': /^(certo|correto|errado|tente de novo)\b/i, en: /^(right|correct|wrong|try again)\b/i } as const;
		const feedback = (l: Lesson, locale: Locale) => l.steps.flatMap((st) => [st.text[locale].right, st.text[locale].wrong, ...Object.values(st.text[locale].feedback ?? {})]);
		for (const l of lessons) for (const locale of LOCALES) for (const fb of feedback(l, locale)) if (fb) expect(fb).not.toMatch(label[locale]);
	});

	it('scores first-try answers and reviews only decision and transfer questions', () => {
		const lesson = byId('foundations-bar');
		const tries = new Map([
			['wick', true],
			['pct', false],
			['known', true]
		]);
		expect(firstTryScore(lesson, tries)).toEqual({ right: 2, total: 5 });
		expect(reviewSteps(lesson).map((s) => s.id)).toEqual(['wick', 'pct', 'known', 'range', 'move']);
		for (const l of lessons) expect(reviewSteps(l).every((s) => s.phase === 'decision' || s.phase === 'transfer')).toBe(true);
	});
});

describe('answer keys match the data they ask about', () => {
	const step = (lesson: string, id: string) => byId(lesson).steps.find((s) => s.id === id)!;
	const answer = (lesson: string, id: string): Choice => {
		const q = step(lesson, id).question!;
		if (q.type === 'number' || q.type === 'set') throw new Error('not a choice');
		return q.choices[q.answer];
	};
	const lowerWick = (c: Candle) => Math.min(c.o, c.c) - c.l;
	const pct = (from: number, to: number) => Math.round(((to - from) / from) * 10_000) / 100;

	it('Foundations (unchanged from academy.ts)', () => {
		const x = step('foundations-bar', 'wick').chart!.candles;
		const down = x.map((c, i) => ({ c, bar: i + 1 })).filter(({ c }) => c.c < c.o);
		const longest = down.reduce((a, b) => (lowerWick(b.c) > lowerWick(a.c) ? b : a));
		expect(answer('foundations-bar', 'wick')).toEqual({ kind: 'bar', bar: longest.bar });
		const b5 = step('foundations-bar', 'pct').chart!.candles[4];
		expect(answer('foundations-bar', 'pct')).toEqual({ kind: 'pct', value: pct(b5.o, b5.c) });
		const y = step('foundations-bar', 'range').chart!.candles;
		const widest = y.reduce((best, c, i) => (c.h - c.l > y[best].h - y[best].l ? i : best), 0);
		expect(answer('foundations-bar', 'range')).toEqual({ kind: 'bar', bar: widest + 1 });
		expect(answer('foundations-bar', 'move')).toEqual({ kind: 'pct', value: pct(y[0].c, y[3].c) });
		expect(answer('foundations-bar', 'known')).toEqual({ kind: 'text', id: 'close6' });
		expect(step('foundations-bar', 'known').chart!.known).toBe(6);
	});

	it('Structure: the first close beyond the previous two bars’ extreme breaks the structure', () => {
		const firstBreak = (cs: Candle[], dir: 1 | -1) =>
			cs.findIndex((c, i) => i >= 2 && (dir < 0 ? c.c < Math.min(cs[i - 1].l, cs[i - 2].l) : c.c > Math.max(cs[i - 1].h, cs[i - 2].h))) + 1;
		expect(answer('structure-zones', 'broke')).toEqual({ kind: 'bar', bar: firstBreak(step('structure-zones', 'broke').chart!.candles, -1) });
		expect(answer('structure-zones', 'transfer-break')).toEqual({ kind: 'bar', bar: firstBreak(step('structure-zones', 'transfer-break').chart!.candles, 1) });
		const known = step('structure-zones', 'held').chart!;
		const lows = known.candles.slice(0, known.known).map((c) => c.l);
		expect(Math.min(...lows)).toBe(98.1);
		expect([...lows].sort((a, b) => a - b)[1]).toBe(98.4);
	});

	it('Patterns: the engulfing bar and the first close below its low', () => {
		const cs = step('patterns-lag', 'spot').chart!.candles;
		const [b4, b5] = [cs[3], cs[4]];
		expect(b4.c < b4.o && b5.o < b4.c && b5.c > b4.o).toBe(true);
		const failed = cs.findIndex((c, i) => i > 4 && c.c < b5.l) + 1;
		expect(answer('patterns-lag', 'spot')).toEqual({ kind: 'bar', bar: failed });
	});

	it('Futures II: the mark never reached the liquidation the last price wicked through', () => {
		const chart = step('futures-margin', 'wick').chart!;
		const level = (k: string) => chart.levels!.find((l) => l.kind === k)!.price;
		const low = Math.min(...chart.candles.map((c) => c.l));
		expect(low).toBeLessThan(level('stop'));
		expect(low).toBeLessThan(level('liquidation'));
		expect(level('mark')).toBeGreaterThan(level('liquidation'));
	});
});

/** Numbers a lesson's texts state that no check, widget, chart, answer or given produces (small integers are counts). */
function uncheckedNumbers(l: Lesson): string[] {
	const known = new Set([...numbersIn(l.checks ?? []), ...(l.givens ?? []), ...l.steps.flatMap((s) => numbersIn([s.chart, s.widget, s.question]))].map(norm));
	const ok = (n: number) => known.has(norm(n)) || (Number.isInteger(n) && n <= 31);
	return LOCALES.flatMap((locale) =>
		allTexts(l, locale).flatMap((text) => statedNumbers(plainText(text, locale), locale).filter((n) => !ok(n)).map((n) => `${l.id} ${locale}: ${n} in "${text}"`))
	);
}

// Numbers as written in a locale ("2.400,5" in PT-BR, "2,400.5" in en), without their sign.
function statedNumbers(text: string, locale: Locale): number[] {
	const re = locale === 'pt-BR' ? /\d{1,3}(?:\.\d{3})+(?:,\d+)?|\d+(?:,\d+)?/g : /\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?/g;
	return [...text.matchAll(re)].map((m) => Number(locale === 'pt-BR' ? m[0].replace(/\./g, '').replace(',', '.') : m[0].replace(/,/g, '')));
}

function norm(n: number): string {
	return String(Math.round(Math.abs(n) * 1e6) / 1e6);
}
