import { describe, expect, it } from 'vitest';
import { STAGES } from './curriculum.ts';
import { firstTryScore, isQuestion, lessonById, LESSONS, lessonsOf, lessonText, type Candle, type Choice } from './academy.ts';

const lesson = lessonById('foundations-bar')!;
const step = (id: string) => lesson.steps.find((s) => s.id === id)!;
const answer = (id: string): Choice => {
	const q = step(id).question!;
	return q.choices[q.answer];
};
const lowerWick = (c: Candle) => Math.min(c.o, c.c) - c.l;
const pct = (from: number, to: number) => Math.round(((to - from) / from) * 10_000) / 100;

describe('Academy lessons', () => {
	it('every stage lesson exists and every lesson belongs to a stage that lists it', () => {
		for (const s of STAGES) for (const id of s.lessons) expect(lessonById(id)?.stage).toBe(s.id);
		for (const l of LESSONS) expect(STAGES.find((s) => s.id === l.stage)?.lessons).toContain(l.id);
		expect(lessonsOf('foundations').map((l) => l.id)).toEqual(['foundations-bar']);
	});

	it('follows concept, demonstration, decision and transfer, in that order (feedback comes with each answer)', () => {
		const order = ['concept', 'demo', 'decision', 'transfer'];
		const phases = lesson.steps.map((s) => s.phase);
		expect(new Set(phases)).toEqual(new Set(order));
		expect([...phases].sort((a, b) => order.indexOf(a) - order.indexOf(b))).toEqual(phases);
		for (const s of lesson.steps.filter(isQuestion)) expect(['decision', 'transfer']).toContain(s.phase);
	});

	it('Transfer on new market: the transfer charts use a market and window the demonstration did not show', () => {
		const shown = lesson.steps.filter((s) => s.phase !== 'transfer').map((s) => s.chart);
		const transfer = lesson.steps.filter((s) => s.phase === 'transfer').map((s) => s.chart);
		expect(transfer.length).toBeGreaterThan(0);
		for (const t of transfer) {
			for (const d of shown) {
				expect(t!.market).not.toBe(d!.market);
				for (const c of t!.candles) expect(d!.candles).not.toContainEqual(c);
			}
		}
	});

	it('answer keys match the chart data', () => {
		const x = step('wick').chart!.candles;
		const down = x.map((c, i) => ({ c, bar: i + 1 })).filter(({ c }) => c.c < c.o);
		const longest = down.reduce((a, b) => (lowerWick(b.c) > lowerWick(a.c) ? b : a));
		expect(answer('wick')).toEqual({ kind: 'bar', bar: longest.bar });

		const b5 = step('pct').chart!.candles[4];
		expect(answer('pct')).toEqual({ kind: 'pct', value: pct(b5.o, b5.c) });

		const y = step('range').chart!.candles;
		const widest = y.reduce((best, c, i) => (c.h - c.l > y[best].h - y[best].l ? i : best), 0);
		expect(answer('range')).toEqual({ kind: 'bar', bar: widest + 1 });
		expect(answer('move')).toEqual({ kind: 'pct', value: pct(y[0].c, y[3].c) });

		expect(answer('known')).toEqual({ kind: 'text', id: 'close6' });
		expect(step('known').chart!.known).toBe(6);
	});

	it('every OHLC bar is consistent (high and low contain the body)', () => {
		for (const l of LESSONS)
			for (const s of l.steps)
				for (const c of s.chart?.candles ?? []) {
					expect(c.h).toBeGreaterThanOrEqual(Math.max(c.o, c.c));
					expect(c.l).toBeLessThanOrEqual(Math.min(c.o, c.c));
				}
	});

	it('has PT-BR and en texts for every step, feedback for every question and every text choice', () => {
		for (const l of LESSONS)
			for (const locale of ['pt-BR', 'en'] as const) {
				const tx = lessonText(l.id, locale);
				expect(tx.title).not.toBe('');
				for (const s of l.steps) {
					const st = tx.steps[s.id];
					expect(st?.title, `${locale} ${s.id}`).toBeTruthy();
					if (!s.question) continue;
					expect(st.right && st.wrong).toBeTruthy();
					for (const c of s.question.choices) if (c.kind === 'text') expect(st.choices?.[c.id]).toBeTruthy();
				}
			}
	});

	it('PT-BR texts write decimals with a comma', () => {
		const pt = JSON.stringify(lessonText('foundations-bar', 'pt-BR'));
		expect(pt).not.toMatch(/\d\.\d{2}\b/);
		expect(pt).toContain('100,80');
	});

	it('scores first-try answers', () => {
		const tries = new Map([
			['wick', true],
			['pct', false],
			['known', true]
		]);
		expect(firstTryScore(lesson, tries)).toEqual({ right: 2, total: 5 });
	});

	// Playtest: the feedback line already starts with "Right." / "Correto."; a text that opened with
	// "Right:" or "Certo:" read "Right. Right: …".
	it('feedback texts never repeat the right/wrong label the runner puts before them', () => {
		const label = { 'pt-BR': /^(certo|correto|errado|tente de novo)\b/i, en: /^(right|correct|wrong|try again)\b/i } as const;
		for (const l of LESSONS) {
			for (const locale of ['pt-BR', 'en'] as const) {
				for (const st of Object.values(lessonText(l.id, locale).steps)) {
					for (const fb of [st.right, st.wrong]) if (fb) expect(fb).not.toMatch(label[locale]);
				}
			}
		}
	});
});
