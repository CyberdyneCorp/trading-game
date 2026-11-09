import { describe, expect, it } from 'vitest';
import { DRILL_SKILLS, DRILL_TEMPLATES, skillsForLessons, templateProblems, templatesOf, type DrillTemplate } from '#lib/content/drills.ts';
import { lessonMeta } from '#lib/content/academy.ts';
import {
	buildSession,
	chooseSkills,
	DRILL_ITEMS,
	drawParams,
	drillSeed,
	finishSession,
	isRight,
	itemText,
	makeItem,
	parseAnswer,
	scheduleSkills,
	sessionIndex,
	type DrillItem,
	type SessionInput
} from './drills.ts';
import { mulberry32 } from './prng.ts';
import { EMPTY_PROGRESS, migrateProgress, type Progress } from './storage.ts';

const LOCALES = ['pt-BR', 'en'] as const;

// An independent hand formula per answer kind (the item's answer comes from the lesson checks).
function byHand(t: DrillTemplate, p: Record<string, number>): number {
	const a = t.answer as Record<string, unknown>;
	const legs = (a.legs ?? []) as { kind: string; side: string; strike: string; price: string }[];
	const val = (v: string) => p[v.slice(1)];
	const legAt = (l: (typeof legs)[number], s: number) => {
		const k = val(l.strike);
		const intrinsic = l.kind === 'call' ? Math.max(s - k, 0) : Math.max(k - s, 0);
		return (l.side === 'buy' ? 1 : -1) * (intrinsic - val(l.price));
	};
	switch (a.kind) {
		case 'payoff':
			return legs.reduce((sum, l) => sum + legAt(l, p.S), 0);
		case 'breakeven': {
			const cost = legs.reduce((sum, l) => sum + val(l.price), 0);
			return a.index === 1 ? p.K + cost : p.K - cost;
		}
		case 'hedge':
			return p.d * p.n;
		case 'scalp':
			return 0.5 * p.g * p.m * p.m * p.n;
		case 'size':
			return (p.equity * p.risk) / 100 / (p.entry - p.stop);
		case 'liquidation':
			return a.side === 'buy' ? (p.entry - p.entry / p.lev) / (1 - 0.005) : (p.entry + p.entry / p.lev) / (1 + 0.005);
		case 'funding':
			return (p.notional * p.rate) / 100;
		case 'r':
			return (p.target - p.entry) / (p.entry - p.stop);
		case 'calendar':
			return (p.far - p.near) * p.qty;
		case 'trail': {
			const cents = Math.round(p.close * 100);
			const dist = p.dist != null ? Math.round(p.dist * 100) : Math.ceil((cents * p.rate) / 100 - 1e-9);
			return (cents - dist) / 100;
		}
		case 'choice':
			return (a.answers as number[])[p.case];
	}
	throw new Error(`no hand formula for ${String(a.kind)}`);
}

const nums = (params: Record<string, string>) => Object.fromEntries(Object.entries(params).map(([k, v]) => [k, Number(v)]));

describe('drill content (design D15)', () => {
	it('covers the eleven skills, every template valid, every lesson real', () => {
		expect(DRILL_SKILLS).toHaveLength(11);
		for (const s of DRILL_SKILLS) expect(templatesOf(s).length).toBeGreaterThan(0);
		for (const t of DRILL_TEMPLATES) {
			expect(templateProblems(t)).toEqual([]);
			for (const l of t.lessons) expect(lessonMeta(l), `${t.id}: ${l}`).not.toBeNull();
			expect(lessonMeta(t.lessons[0])?.stage).toBe(t.stage);
		}
		expect(new Set(DRILL_TEMPLATES.map((t) => t.id)).size).toBe(DRILL_TEMPLATES.length);
	});

	it('refuses broken templates', () => {
		const t = DRILL_TEMPLATES[0];
		expect(templateProblems({ ...t, params: { x: { lo: '5', hi: '1', step: '1' } } })).toEqual([`${t.id}.x: bad range`]);
		expect(templateProblems({ ...t, params: { x: { add: ['a', 'b'] } } })).toEqual([`${t.id}.x: unknown operand`]);
		expect(templateProblems({ ...t, lessons: [] })).toEqual([`${t.id}: no lesson`]);
	});

	it('no mission id in a drill', () => {
		expect(JSON.stringify(DRILL_TEMPLATES)).not.toMatch(/\bm\d\d-|\br0\d-/);
	});

	it('an explanation states no number of its own: digits outside placeholders are only 0 or 1', () => {
		for (const t of DRILL_TEMPLATES) {
			for (const l of LOCALES) {
				const literal = t.text[l].explain.replace(/\{\w+\}/g, '').match(/\d+([.,]\d+)?/g) ?? [];
				expect(literal.filter((d) => d !== '0' && d !== '1'), `${t.id} ${l}`).toEqual([]);
			}
		}
	});

	it('200 sessions per template: parameters on their grid, answers equal the hand formula, the explanation shows the answer', () => {
		for (const t of DRILL_TEMPLATES) {
			for (let s = 1; s <= 200; s++) {
				const it = makeItem(t, mulberry32(s * 7919));
				for (const [name, spec] of Object.entries(t.params)) {
					if (!('lo' in spec)) continue;
					const k = (Number(it.params[name]) - Number(spec.lo)) / Number(spec.step);
					expect(Math.abs(k - Math.round(k)), `${t.id}.${name}=${it.params[name]}`).toBeLessThan(1e-9);
					expect(Number(it.params[name])).toBeLessThanOrEqual(Number(spec.hi));
				}
				expect(Number.isFinite(it.answer), t.id).toBe(true);
				const hand = byHand(t, nums(it.params));
				expect(Math.abs(it.answer - hand), `${t.id} ${JSON.stringify(it.params)}`).toBeLessThanOrEqual(it.choice ? 0 : 0.005 + 1e-9);
				for (const l of LOCALES) {
					const text = itemText(it, t, l);
					expect(text.prompt).not.toMatch(/\{\w+\}/);
					expect(text.explain).not.toMatch(/\{\w+\}/);
					if (!it.choice) expect(text.explain, `${t.id} ${l}`).toContain(text.answer);
				}
			}
		}
	});

	it('builds decimals from integer multiples of the step (no float products)', () => {
		const t = templatesOf('calendar')[0];
		const p = drawParams(t, mulberry32(3));
		expect(p.far).toBe(String(Math.round((Number(p.near) + Number(p.extra)) * 10) / 10));
		expect(Object.keys(p)).toEqual(['near', 'extra', 'qty', 'far']); // drawn in the file's key order
	});
});

describe('answers', () => {
	const item = (answer: number, choice = false): DrillItem => ({
		template: 'x',
		skill: 'payoff',
		lessons: [],
		params: {},
		answer,
		digits: 2,
		tolerance: 0.005,
		choice
	});
	it('accepts a comma or a dot, within half a cent', () => {
		expect(parseAnswer('12,5')).toBe(12.5);
		expect(parseAnswer(' -3.25 ')).toBe(-3.25);
		expect(Number.isNaN(parseAnswer('abc'))).toBe(true);
		expect(isRight(item(12.5), 12.5)).toBe(true);
		expect(isRight(item(12.5), 12.504)).toBe(true);
		expect(isRight(item(12.5), 12.51)).toBe(false);
		expect(isRight(item(1, true), 1)).toBe(true);
		expect(isRight(item(1, true), 0)).toBe(false);
	});
});

const NOW = new Date(2026, 10, 9, 10).getTime();
const input = (over: Partial<SessionInput> = {}): SessionInput => ({
	date: '2026-11-09',
	now: NOW,
	session: 0,
	lessonReviews: {},
	drillReviews: {},
	history: [],
	...over
});

describe('session choice and scheduling (fake clock)', () => {
	it('Due lesson first: options-payoff due today brings a payoff or breakeven item', () => {
		const items = buildSession(input({ lessonReviews: { 'options-payoff': { box: 1, due: '2026-11-09' } } }));
		expect(items).toHaveLength(DRILL_ITEMS);
		expect(['payoff', 'breakeven']).toContain(items[0].skill);
		expect(skillsForLessons(['options-payoff'])).toEqual(['payoff', 'breakeven']);
	});

	it('at most 3 from due lessons, then due skills, then the weakest first-try rate', () => {
		const rank = DRILL_SKILLS.map((_, i) => i);
		const due = { 'options-payoff': { box: 1, due: '2026-11-01' }, 'risk-sizing': { box: 1, due: '2026-11-01' }, 'options-greeks': { box: 1, due: '2026-11-02' } };
		const skills = chooseSkills(
			input({
				lessonReviews: due,
				drillReviews: { trail: { box: 2, due: NOW - 1 }, funding: { box: 3, due: NOW + 86_400_000 } },
				history: [{ date: '2026-11-08', items: 6, firstTry: 0, skills: { calendar: { n: 2, right: 0 } } }]
			}),
			rank
		);
		expect(skills.slice(0, 3)).toEqual(['payoff', 'breakeven', 'greeks-read']);
		expect(skills[3]).toBe('trail'); // due in the drill schedule
		expect(skills[4]).toBe('calendar'); // 0% on the first try
		expect(skills).not.toContain('funding'); // not due yet, and an unseen skill counts 50%
		expect(new Set(skills).size).toBe(DRILL_ITEMS);
	});

	it('the same date and session give the same items; the next session others', () => {
		expect(buildSession(input())).toEqual(buildSession(input()));
		expect(buildSession(input({ session: 1 }))).not.toEqual(buildSession(input()));
		expect(drillSeed('2026-11-09', 0)).not.toBe(drillSeed('2026-11-09', 1));
		expect(sessionIndex([{ date: '2026-11-09', items: 6, firstTry: 3 }], '2026-11-09')).toBe(1);
	});

	it('all right moves a skill up a box, any wrong sends it to box 1, skipped changes nothing', () => {
		const items = buildSession(input()).slice(0, 3);
		const day = (d: number) => new Date(2026, 10, 9 + d).getTime();
		const before = { [items[2].skill]: { box: 3, due: day(0) } };
		const r = scheduleSkills(before, items, ['right', 'wrong', 'skipped'], '2026-11-09');
		expect(r[items[0].skill]).toEqual({ box: 1 + 1, due: day(3) }); // unseen, all right: box 2 in 3 days
		expect(r[items[1].skill]).toEqual({ box: 1, due: day(1) });
		expect(r[items[2].skill]).toEqual(before[items[2].skill]);
	});

	it('a finished session keeps its history (30 at most) and never completes a lesson review', () => {
		const items = buildSession(input());
		const reviews = { 'options-payoff': { box: 1, due: '2026-11-09' } };
		let p: Progress = { ...EMPTY_PROGRESS, lessonReviews: reviews };
		for (let i = 0; i < 32; i++) p = finishSession(p, items, items.map(() => 'right'), '2026-11-09');
		expect(p.drillHistory).toHaveLength(30);
		expect(p.drillHistory[0]).toMatchObject({ date: '2026-11-09', items: 6, firstTry: 6 });
		expect(p.lessonReviews).toEqual(reviews);
		expect(migrateProgress(JSON.parse(JSON.stringify(p))).drillHistory).toEqual(p.drillHistory);
	});

	it('Time runs out: two unanswered items are skipped, no penalty beyond that', () => {
		const items = buildSession(input());
		const results = ['right', 'right', 'wrong', 'right', 'skipped', 'skipped'] as const;
		const p = finishSession({ ...EMPTY_PROGRESS }, items, results, '2026-11-09');
		expect(p.drillHistory[0].firstTry).toBe(3);
		expect(Object.values(p.drillHistory[0].skills ?? {}).reduce((a, s) => a + s.n, 0)).toBe(4);
		expect(p.drillReviews[items[4].skill]).toBeUndefined();
	});
});
