// The desktop screens' texts and data that the web keeps outside i18n.svelte.ts (objectives, debrief
// findings, news names, Academy lessons) and the web's screen rules as data (curriculum stages,
// campaign constants, default shortcuts, lesson steps). Read with the web's TypeScript toolchain, like
// the rest of the catalog: never retyped. Every source path is optional (absent = skipped), so the
// small fixtures of the script tests keep working.

import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const DESKTOP_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WEB_GAME = path.resolve(DESKTOP_DIR, '../web/src/lib/game');

export const SCREEN_PATHS = {
	webAnalysis: path.join(WEB_GAME, 'analysis.ts'),
	webAcademy: path.join(WEB_GAME, 'academy.ts'),
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

function academyRows(ts, file, readConsts, out) {
	const c = readConsts(ts, file);
	for (const [id, byLocale] of Object.entries(c.get('TEXTS'))) {
		const [pt, en] = LOCALES.map((l) => byLocale[l] ?? { steps: {} });
		const base = `academyLesson.${id}`;
		out.push([`${base}.title`, pt.title, en.title, 'web academy'], [`${base}.summary`, pt.summary, en.summary, 'web academy']);
		for (const [step, text] of Object.entries(pt.steps)) {
			const other = en.steps[step] ?? {};
			for (const field of ['title', 'body', 'right', 'wrong']) {
				if (text[field] !== undefined) out.push([`${base}.${step}.${field}`, text[field], other[field] ?? '', 'web academy']);
			}
			for (const [choice, label] of Object.entries(text.choices ?? {})) {
				out.push([`${base}.${step}.choice.${choice}`, label, other.choices?.[choice] ?? '', 'web academy']);
			}
		}
	}
	return c.duplicates;
}

// objectives.ts (objective.*) and news.ts (news.kind.*, news.level.*) are read once, by i18n_game.mjs.
const ROW_SOURCES = [
	['webAnalysis', analysisRows],
	['webAcademy', academyRows]
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

/**
 * academy.ts LESSONS is built with helpers (x(), .map()), not a literal: transpile the module and run
 * it (it only imports types, which transpiling removes) to read the data it exports.
 */
function evaluateModule(ts, file) {
	const js = ts.transpileModule(readFileSync(file, 'utf8'), {
		compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 }
	}).outputText;
	const module = { exports: {} };
	new Function('module', 'exports', 'require', js)(module, module.exports, (name) => {
		throw new Error(`${path.basename(file)}: runtime import of ${name} is not supported`);
	});
	return module.exports;
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
		lessons: evaluateModule(ts, paths.webAcademy).LESSONS
	};
}

const CONTENT_KEYS = ['webCurriculum', 'webCampaign', 'webShortcuts', 'webCampaignReport', 'webDebrief', 'webAcademy'];

/** resources/web/content.json text, or null when a source is missing (script-test fixtures). */
export function screenContentJson(ts, paths, readConsts) {
	if (!CONTENT_KEYS.every((k) => has(paths, k)) || !paths.content) return null;
	return JSON.stringify(contentOf(ts, paths, readConsts), null, 1) + '\n';
}
