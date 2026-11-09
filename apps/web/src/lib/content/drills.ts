// Daily drill content (add-practice-and-advanced-strategies design D15): one file per skill in
// ./drills, each holding templates. A template has parameter ranges on a decimal grid (or a list, or the
// sum or difference of two parameters), an answer given as a lesson check kind with "$name" references,
// and per-locale texts with {name} placeholders ({answer} in the explanation). Answers come from the
// same check functions the lesson tests compare with the engine (checks.ts); no drill needs an engine
// call at run time: Greeks are stated as givens, as in the lessons' hedge and scalp checks.

import type { Locale } from '#lib/game/i18n.svelte.ts';

export const DRILL_SKILLS = [
	'payoff',
	'breakeven',
	'greeks-read',
	'greeks-sign',
	'sizing',
	'liquidation',
	'funding',
	'r-multiple',
	'hedge',
	'calendar',
	'trail'
] as const;
export type DrillSkill = (typeof DRILL_SKILLS)[number];

export type ParamSpec = { lo: string; hi: string; step: string } | { of: string[] } | { add: [string, string] } | { sub: [string, string] };

/** A check with "$param" references (resolved per item); `index` picks one breakeven of a list. */
export type AnswerSpec = Record<string, unknown> & {
	kind: string;
	index?: number;
};

/** A multiple-choice template's answer: the right choice for each value of `from`. */
export interface ChoiceSpec {
	kind: 'choice';
	from: string;
	answers: number[];
}

export interface DrillText {
	prompt: string;
	explain: string;
	/** Choice templates: the label of each value of the choice parameter, and the choices. */
	cases?: string[];
	choices?: string[];
}

export interface DrillTemplate {
	id: string;
	skill: DrillSkill;
	stage: string;
	lessons: string[];
	params: Record<string, ParamSpec>;
	answer: AnswerSpec | ChoiceSpec;
	/** Decimals the answer is given to (default 2). */
	digits?: number;
	/** Largest accepted difference (default half a unit of the last digit). */
	tolerance?: number;
	text: Record<Locale, DrillText>;
}

interface DrillFile {
	skill: DrillSkill;
	templates: Omit<DrillTemplate, 'skill'>[];
}

const FILES = import.meta.glob<DrillFile>('./drills/*.json', {
	eager: true,
	import: 'default'
});

const DECIMAL = /^\d+(\.\d+)?$/;
const LOCALES: Locale[] = ['pt-BR', 'en'];

/** Problems of one template against the schema (empty when valid). */
export function templateProblems(t: DrillTemplate): string[] {
	const out: string[] = [];
	if (!DRILL_SKILLS.includes(t.skill)) out.push(`${t.id}: unknown skill ${t.skill}`);
	if (!t.lessons?.length) out.push(`${t.id}: no lesson`);
	for (const [name, p] of Object.entries(t.params)) out.push(...paramProblems(t.id, name, p, t.params));
	for (const l of LOCALES) {
		const x = t.text?.[l];
		if (!x?.prompt || !x.explain) out.push(`${t.id}: ${l} text missing`);
		if (t.answer.kind === 'choice' && (!x?.choices?.length || !x.cases?.length)) out.push(`${t.id}: ${l} choices missing`);
	}
	return out;
}

function paramProblems(id: string, name: string, p: ParamSpec, all: Record<string, ParamSpec>): string[] {
	if ('lo' in p) {
		const ok = DECIMAL.test(p.lo) && DECIMAL.test(p.hi) && DECIMAL.test(p.step) && Number(p.step) > 0 && Number(p.lo) <= Number(p.hi);
		return ok ? [] : [`${id}.${name}: bad range`];
	}
	if ('of' in p) return p.of.length && p.of.every((v) => DECIMAL.test(v)) ? [] : [`${id}.${name}: bad list`];
	const [a, b] = 'add' in p ? p.add : p.sub;
	return a in all && b in all ? [] : [`${id}.${name}: unknown operand`];
}

/** Every template, skill by skill in DRILL_SKILLS order, then by file order. */
export const DRILL_TEMPLATES: DrillTemplate[] = DRILL_SKILLS.flatMap((skill) => {
	const file = FILES[`./drills/${skill}.json`];
	return (file?.templates ?? []).map((t) => ({ ...t, skill }));
});

export const templatesOf = (skill: DrillSkill): DrillTemplate[] => DRILL_TEMPLATES.filter((t) => t.skill === skill);

/** Skills whose templates review one of these lessons. */
export function skillsForLessons(lessons: readonly string[]): DrillSkill[] {
	return DRILL_SKILLS.filter((s) => templatesOf(s).some((t) => t.lessons.some((l) => lessons.includes(l))));
}
