// Terminal texts the web keeps outside i18n.svelte.ts, read with the same data-literal evaluator
// (readConsts) so they are generated, never retyped:
//   news.ts          headlines, kinds and the five-step scale -> news.headline.<kind>.<pos|neu|neg>,
//                    news.kind.<kind>, news.level.<0..4>
//   candlePatterns.ts pattern names and cheat-sheet lines     -> pattern.<id>.<name|what|means|confirm>
//   objectives.ts    mission objectives ({pct})               -> objective.<code>
//   tutorials.ts     pre-mission cards                        -> tutorial.<mission>.<n>.<title|body|art>
//   lessons.ts       lesson questions (q(pt, en) helper)      -> lessonq.<mission>.<id>.<prompt|choice<i>|unit>
// The paths are DEFAULT_PATHS.web* (i18n_lib.mjs); paths absent from `paths` (test fixtures) are skipped.

import { readConsts } from './i18n_lib.mjs';

const PT = 'pt-BR';
const TONES = ['pos', 'neu', 'neg'];

function newsRows(ts, file, rows) {
	const c = readConsts(ts, file);
	const headlines = c.get('headlines');
	for (const kind of Object.keys(headlines[PT])) {
		for (const tone of TONES) rows.push([`news.headline.${kind}.${tone}`, headlines[PT][kind][tone], headlines.en[kind]?.[tone]]);
	}
	const kinds = c.get('kindNames');
	for (const kind of Object.keys(kinds[PT])) rows.push([`news.kind.${kind}`, kinds[PT][kind], kinds.en[kind]]);
	const scale = c.get('scale');
	scale[PT].forEach((text, i) => rows.push([`news.level.${i}`, text, scale.en[i]]));
	return c.duplicates;
}

function patternRows(ts, file, rows) {
	const c = readConsts(ts, file);
	for (const list of ['CANDLE_PATTERNS', 'MORE_CANDLE_PATTERNS']) {
		for (const p of c.get(list)) {
			for (const field of ['name', 'what', 'means', 'confirm']) {
				rows.push([`pattern.${p.id}.${field}`, p.text[PT][field], p.text.en?.[field]]);
			}
		}
	}
	return c.duplicates;
}

function objectiveRows(ts, file, rows) {
	const c = readConsts(ts, file);
	const pt = c.get('PT');
	const en = c.get('EN');
	for (const code of Object.keys(pt)) rows.push([`objective.${code}`, pt[code], en[code]]);
	return c.duplicates;
}

function tutorialRows(ts, file, rows) {
	const c = readConsts(ts, file);
	for (const [mission, cards] of Object.entries(c.get('TUTORIALS'))) {
		cards[PT].forEach((card, i) => {
			const en = cards.en[i] ?? {};
			rows.push([`tutorial.${mission}.${i}.title`, card.title, en.title]);
			rows.push([`tutorial.${mission}.${i}.body`, card.body, en.body]);
			rows.push([`tutorial.${mission}.${i}.art`, card.art, en.art]);
		});
	}
	return c.duplicates;
}

function lessonRows(ts, file, rows) {
	const c = readConsts(ts, file, { calls: { q: ([pt, en]) => ({ [PT]: pt, en }) } });
	for (const [mission, questions] of Object.entries(c.get('TEXTS'))) {
		for (const [id, byLocale] of Object.entries(questions)) {
			const pt = byLocale[PT];
			const en = byLocale.en;
			const base = `lessonq.${mission}.${id}`;
			rows.push([`${base}.prompt`, pt.prompt, en.prompt]);
			pt.choices.forEach((choice, i) => rows.push([`${base}.choice${i}`, choice, en.choices[i]]));
			if (pt.unit || en.unit) rows.push([`${base}.unit`, pt.unit, en.unit]);
		}
	}
	return c.duplicates;
}

const SOURCES = [
	['webNews', 'web news', newsRows],
	['webPatterns', 'web patterns', patternRows],
	['webObjectives', 'web objectives', objectiveRows],
	['webTutorials', 'web tutorials', tutorialRows],
	['webLessons', 'web lessons', lessonRows]
];

/** Rows [key, pt-BR, en, origin] of the game catalogs named in `paths`, and the problems found. */
export function gameRows(ts, paths) {
	const rows = [];
	const problems = [];
	for (const [field, origin, read] of SOURCES) {
		if (!paths[field]) continue;
		const found = [];
		problems.push(...read(ts, paths[field], found));
		for (const [key, pt, en] of found) rows.push([key, pt ?? '', en ?? '', origin]);
	}
	return { rows, problems };
}
