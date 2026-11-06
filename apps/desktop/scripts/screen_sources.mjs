// The desktop screens' texts and data that the web keeps outside i18n.svelte.ts (objectives, debrief
// findings, news names, Academy lessons) and the web's screen rules as data (curriculum stages,
// campaign constants, default shortcuts, lesson steps). Read with the web's TypeScript toolchain, like
// the rest of the catalog, and the Academy lessons straight from the web's JSON content files
// (apps/web/src/lib/content/academy, add-academy-and-strategies design D1): never retyped. Every
// source path is optional (absent = skipped), so the small fixtures of the script tests keep working.

import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const DESKTOP_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WEB_GAME = path.resolve(DESKTOP_DIR, '../web/src/lib/game');
const WEB_CONTENT = path.resolve(DESKTOP_DIR, '../web/src/lib/content');

export const SCREEN_PATHS = {
	webAnalysis: path.join(WEB_GAME, 'analysis.ts'),
	webAcademy: path.join(WEB_CONTENT, 'academy'),
	webGlossary: path.join(WEB_CONTENT, 'glossary.json'),
	webGlossaryTs: path.join(WEB_CONTENT, 'glossary.ts'),
	webCoach: path.join(WEB_CONTENT, 'coach.json'),
	webCurriculum: path.join(WEB_GAME, 'curriculum.ts'),
	webCampaign: path.join(WEB_GAME, 'campaign.ts'),
	webCampaignReport: path.join(WEB_GAME, 'campaignReport.ts'),
	webShortcuts: path.join(WEB_GAME, 'shortcuts.ts'),
	webDebrief: path.join(WEB_GAME, 'debrief.ts'),
	content: path.join(DESKTOP_DIR, 'resources/web/content.json')
};

const LOCALES = ['pt-BR', 'en'];
const has = (paths, key) => paths[key] && existsSync(paths[key]);

/** Adds `prefix.<key>` rows from a { 'pt-BR': {k: text}, en: {k: text} } table. */
function addTable(out, table, prefix, origin, suffix = '') {
	for (const key of Object.keys(table['pt-BR'] ?? {})) {
		out.push([`${prefix}.${key}${suffix}`, table['pt-BR'][key], table.en?.[key] ?? '', origin]);
	}
}

function analysisRows(ts, file, readConsts, out) {
	const c = readConsts(ts, file);
	addTable(out, c.get('findingText'), 'finding', 'web analysis');
	addTable(out, c.get('safeLeverageText'), 'finding', 'web analysis', '.safe');
	addTable(out, c.get('singularText'), 'finding', 'web analysis', '.one');
	const only = c.get('liquidatedOnlyText');
	out.push(['finding.liquidatedOnly', only['pt-BR'], only.en, 'web analysis']);
	addTable(out, c.get('lessonText'), 'nextLesson', 'web analysis');
	return c.duplicates;
}

const LINK = /\[\[([a-z0-9-]+)(?:\|([^\]]+))?\]\]/g;

/** Glossary ids a step's PT-BR texts link, in order of first appearance (the desktop lists them as terms). */
export function stepTerms(step) {
	const pt = step.text?.['pt-BR'] ?? {};
	const texts = [pt.body, pt.right, pt.wrong, ...Object.values(pt.feedback ?? {}), ...Object.values(pt.choices ?? {})];
	return [...new Set(texts.flatMap((t) => (typeof t === 'string' ? [...t.matchAll(LINK)].map((m) => m[1]) : [])))];
}

/** Lesson and tutorial texts link glossary terms as [[id|text]]; the desktop shows the plain text. */
export const stripLinks = (text) =>
	typeof text === 'string' ? text.replace(/\[\[([a-z0-9-]+)(?:\|([^\]]+))?\]\]/g, (_, id, shown) => shown ?? id) : text;

const readJson = (file) => JSON.parse(readFileSync(file, 'utf8'));

/** The Academy lessons in index order, with chart references (`{"ref": …}`) resolved like the web's loader. */
function academyLessons(dir) {
	return readJson(path.join(dir, 'index.json')).lessons.map((meta) => {
		const file = readJson(path.join(dir, `${meta.id}.json`));
		const steps = file.steps.map((s) => {
			if (!s.chart) return s;
			const { ref, ...own } = s.chart;
			return { ...s, chart: { ...(ref ? file.charts?.[ref] : {}), ...own } };
		});
		return { meta, lesson: { id: file.id, stage: file.stage, steps } };
	});
}

const STEP_FIELDS = ['title', 'body', 'right', 'wrong'];
const STEP_MAPS = { choices: 'choice', feedback: 'feedback', cells: 'cell' };

function stepRows(base, step, out) {
	const [pt, en] = LOCALES.map((l) => step.text[l] ?? {});
	for (const field of STEP_FIELDS) {
		if (pt[field] !== undefined) out.push([`${base}.${field}`, stripLinks(pt[field]), stripLinks(en[field] ?? ''), 'web academy']);
	}
	for (const [map, key] of Object.entries(STEP_MAPS)) {
		for (const [id, label] of Object.entries(pt[map] ?? {})) {
			out.push([`${base}.${key}.${id}`, stripLinks(label), stripLinks(en[map]?.[id] ?? ''), 'web academy']);
		}
	}
	(pt.columns ?? []).forEach((label, i) => out.push([`${base}.column.${i}`, label, en.columns?.[i] ?? '', 'web academy']));
}

function academyRows(_ts, dir, _readConsts, out) {
	for (const { meta, lesson } of academyLessons(dir)) {
		const base = `academyLesson.${meta.id}`;
		out.push([`${base}.title`, meta.title['pt-BR'], meta.title.en, 'web academy'], [`${base}.summary`, meta.summary['pt-BR'], meta.summary.en, 'web academy']);
		for (const step of lesson.steps) stepRows(`${base}.${step.id}`, step, out);
	}
	return [];
}

const TERM_FIELDS = ['term', 'short', 'long', 'example'];

/** glossary.json (design D7) -> glossary.<id>.term/short/long/example and .aliases ("; "-joined). */
function glossaryRows(_ts, file, _readConsts, out) {
	for (const term of readJson(file).terms) {
		const [pt, en] = LOCALES.map((l) => term.text[l] ?? {});
		const base = `glossary.${term.id}`;
		for (const field of TERM_FIELDS) {
			if (pt[field] !== undefined) out.push([`${base}.${field}`, stripLinks(pt[field]), stripLinks(en[field] ?? ''), 'web glossary']);
		}
		out.push([`${base}.aliases`, (pt.aliases ?? []).join('; '), (en.aliases ?? []).join('; '), 'web glossary']);
	}
	return [];
}

/** coach.json (design D6) -> coach.<step>.title/body. */
function coachRows(_ts, file, _readConsts, out) {
	for (const step of readJson(file).steps) {
		const [pt, en] = LOCALES.map((l) => step.text[l] ?? {});
		for (const field of ['title', 'body']) out.push([`coach.${step.id}.${field}`, stripLinks(pt[field]), stripLinks(en[field] ?? ''), 'web coach']);
	}
	return [];
}

// objectives.ts (objective.*) and news.ts (news.kind.*, news.level.*) are read once, by i18n_game.mjs.
const ROW_SOURCES = [
	['webAnalysis', analysisRows],
	['webAcademy', academyRows],
	['webGlossary', glossaryRows],
	['webCoach', coachRows]
];

/** Catalog rows [key, pt-BR, en, origin] from the optional screen sources, and their problems. */
export function screenRows(ts, paths, readConsts) {
	const rows = [];
	const problems = [];
	for (const [key, read] of ROW_SOURCES) {
		if (has(paths, key)) problems.push(...read(ts, paths[key], readConsts, rows));
	}
	return { rows, problems };
}

function contentOf(ts, paths, readConsts) {
	const curriculum = readConsts(ts, paths.webCurriculum);
	const campaign = readConsts(ts, paths.webCampaign);
	const shortcuts = readConsts(ts, paths.webShortcuts);
	return {
		stages: curriculum.get('STAGES'),
		lockedLeverageCap: curriculum.get('LOCKED_LEVERAGE_CAP'),
		campaignCapital: campaign.get('CAMPAIGN_CAPITAL'),
		campaignMinCapital: campaign.get('CAMPAIGN_MIN_CAPITAL'),
		campaignHistory: campaign.get('KEEP_HISTORY'),
		reportMinTrades: readConsts(ts, paths.webCampaignReport).get('REPORT_MIN_TRADES'),
		afterReleaseBars: readConsts(ts, paths.webDebrief).get('AFTER_RELEASE_BARS'),
		shortcutActions: shortcuts.get('SHORTCUT_ACTIONS'),
		defaultKeymap: shortcuts.get('DEFAULT_KEYMAP'),
		// Structure only: the texts are the academyLesson.* rows.
		lessons: academyLessons(paths.webAcademy).map(({ lesson }) => ({
			...lesson,
			steps: lesson.steps.map((step) => {
				const { text: _text, ...rest } = step;
				const terms = stepTerms(step);
				return terms.length ? { ...rest, terms } : rest;
			})
		})),
		// Structure only: the texts are the glossary.* and coach.* rows.
		glossary: has(paths, 'webGlossary') ? readJson(paths.webGlossary).terms.map(({ id, stage, related }) => ({ id, stage, related })) : [],
		coach: has(paths, 'webCoach') ? readJson(paths.webCoach).steps.map(({ id, target, completes }) => ({ id, target, completes })) : [],
		// The glossary term each debrief finding is about (glossary.ts FINDING_TERMS).
		findingTerms: has(paths, 'webGlossaryTs') ? readConsts(ts, paths.webGlossaryTs).get('FINDING_TERMS') : {}
	};
}

const CONTENT_KEYS = ['webCurriculum', 'webCampaign', 'webShortcuts', 'webCampaignReport', 'webDebrief', 'webAcademy'];

/** resources/web/content.json text, or null when a source is missing (script-test fixtures). */
export function screenContentJson(ts, paths, readConsts) {
	if (!CONTENT_KEYS.every((k) => has(paths, k)) || !paths.content) return null;
	return JSON.stringify(contentOf(ts, paths, readConsts), null, 1) + '\n';
}
