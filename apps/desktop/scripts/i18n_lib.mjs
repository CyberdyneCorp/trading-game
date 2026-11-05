// Shared by gen_i18n.mjs and check_i18n.mjs: reads the web's catalogs through the TypeScript compiler
// API (the web's own toolchain, apps/web/node_modules/typescript) instead of regex-parsing TypeScript,
// and builds the desktop catalog (Raven locale CSV "key,pt-BR,en") plus the pinned publisher keys.

import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { gameRows } from './i18n_game.mjs';

export const DESKTOP_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const REPO_DIR = path.resolve(DESKTOP_DIR, '../..');

export const DEFAULT_PATHS = {
	webI18n: path.join(REPO_DIR, 'apps/web/src/lib/game/i18n.svelte.ts'),
	webMissions: path.join(REPO_DIR, 'apps/web/src/lib/game/missions.ts'),
	webPacks: path.join(REPO_DIR, 'apps/web/src/lib/game/packs.ts'),
	desktopJson: path.join(DESKTOP_DIR, 'resources/i18n/desktop.json'),
	csv: path.join(DESKTOP_DIR, 'resources/i18n/strings.csv'),
	pinnedKeys: path.join(DESKTOP_DIR, 'resources/packs/pinned_keys.json'),
	// Terminal texts outside i18n.svelte.ts (i18n_game.mjs).
	webNews: path.join(REPO_DIR, 'apps/web/src/lib/game/news.ts'),
	webPatterns: path.join(REPO_DIR, 'apps/web/src/lib/game/candlePatterns.ts'),
	webObjectives: path.join(REPO_DIR, 'apps/web/src/lib/game/objectives.ts'),
	webTutorials: path.join(REPO_DIR, 'apps/web/src/lib/game/tutorials.ts'),
	webLessons: path.join(REPO_DIR, 'apps/web/src/lib/game/lessons.ts')
};

const LOCALES = ['pt-BR', 'en'];
const MISSION_FIELDS = ['title', 'tagline', 'topic', 'reveal'];

/** The web's TypeScript compiler (npm ci in apps/web provides it). */
export function loadTypeScript() {
	const require = createRequire(path.join(REPO_DIR, 'apps/web/package.json'));
	try {
		return require('typescript');
	} catch {
		throw new Error('typescript not found: run `npm ci` in apps/web (the generator uses the web toolchain)');
	}
}

/**
 * Evaluates the initializer of top-level `const <name>` in a TypeScript file. Only data literals are
 * accepted (strings, template strings without substitutions, `+` of strings, objects, arrays,
 * references to other top-level consts, `as`/`satisfies` wrappers); anything else throws, so a
 * computed catalog entry is never silently dropped. Top-level `name['key'] = <literal>;` statements
 * (missions.ts adds missions that way) are applied in order. Duplicate keys are reported.
 */
export function readConsts(ts, file, { calls = {} } = {}) {
	const source = ts.createSourceFile(file, readFileSync(file, 'utf8'), ts.ScriptTarget.Latest, true);
	const decls = new Map();
	const assignments = new Map(); // name -> [[key, initializer node]]
	for (const stmt of source.statements) {
		if (ts.isVariableStatement(stmt)) {
			for (const d of stmt.declarationList.declarations) {
				if (ts.isIdentifier(d.name) && d.initializer) decls.set(d.name.text, d.initializer);
			}
			continue;
		}
		const keyed = keyedAssignment(ts, stmt);
		if (keyed) {
			if (!assignments.has(keyed.name)) assignments.set(keyed.name, []);
			assignments.get(keyed.name).push([keyed.key, keyed.value]);
		}
	}
	const duplicates = [];
	const where = (node) => {
		const { line } = source.getLineAndCharacterOfPosition(node.getStart());
		return `${path.basename(file)}:${line + 1}`;
	};
	const evaluate = (node) => {
		if (ts.isAsExpression(node) || ts.isParenthesizedExpression(node) || ts.isSatisfiesExpression(node)) {
			return evaluate(node.expression);
		}
		if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) return node.text;
		if (ts.isBinaryExpression(node) && node.operatorToken.kind === ts.SyntaxKind.PlusToken) {
			return evaluate(node.left) + evaluate(node.right);
		}
		if (ts.isArrayLiteralExpression(node)) return node.elements.map(evaluate);
		if (ts.isIdentifier(node) && decls.has(node.text)) return evaluate(decls.get(node.text));
		if (ts.isObjectLiteralExpression(node)) return evaluateObject(node);
		const scalar = evaluateScalar(ts, node);
		if (scalar !== undefined) return scalar.value;
		// Reuse of another literal: m01['pt-BR'][0], m01.en[0] (tutorials.ts).
		if (ts.isElementAccessExpression(node) || ts.isPropertyAccessExpression(node)) {
			const key = ts.isPropertyAccessExpression(node) ? node.name.text : evaluate(node.argumentExpression);
			const value = evaluate(node.expression)?.[key];
			if (value === undefined) throw new Error(`${where(node)}: no such element`);
			return value;
		}
		// A call of a helper the caller declared (lessons.ts `q(pt, en)`), with data-literal arguments.
		if (ts.isCallExpression(node) && ts.isIdentifier(node.expression) && Object.hasOwn(calls, node.expression.text)) {
			return calls[node.expression.text](node.arguments.map(evaluate));
		}
		throw new Error(`${where(node)}: not a data literal (${ts.SyntaxKind[node.kind]})`);
	};
	const evaluateObject = (node) => {
		const out = {};
		for (const p of node.properties) {
			if (!ts.isPropertyAssignment(p)) throw new Error(`${where(p)}: unsupported property`);
			const key = ts.isIdentifier(p.name) || ts.isStringLiteral(p.name) ? p.name.text : null;
			if (key === null) throw new Error(`${where(p)}: computed property name`);
			if (Object.hasOwn(out, key)) duplicates.push(`${where(p)}: duplicate key '${key}'`);
			out[key] = evaluate(p.initializer);
		}
		return out;
	};
	return {
		get(name) {
			if (!decls.has(name)) throw new Error(`${path.basename(file)}: no top-level const ${name}`);
			const value = evaluate(decls.get(name));
			for (const [key, node] of assignments.get(name) ?? []) {
				if (Object.hasOwn(value, key)) duplicates.push(`${where(node)}: duplicate key '${key}'`);
				value[key] = evaluate(node);
			}
			return value;
		},
		duplicates
	};
}

/** Numbers (with a leading minus), true, false and null as { value }; undefined for anything else. */
function evaluateScalar(ts, node) {
	if (ts.isNumericLiteral(node)) return { value: Number(node.text) };
	if (ts.isPrefixUnaryExpression(node) && node.operator === ts.SyntaxKind.MinusToken && ts.isNumericLiteral(node.operand)) {
		return { value: -Number(node.operand.text) };
	}
	if (node.kind === ts.SyntaxKind.TrueKeyword) return { value: true };
	if (node.kind === ts.SyntaxKind.FalseKeyword) return { value: false };
	if (node.kind === ts.SyntaxKind.NullKeyword) return { value: null };
	return undefined;
}

/** `name['key'] = value;` or `name.key = value;` at the top level, else null. */
function keyedAssignment(ts, stmt) {
	if (!ts.isExpressionStatement(stmt)) return null;
	const e = stmt.expression;
	if (!ts.isBinaryExpression(e) || e.operatorToken.kind !== ts.SyntaxKind.EqualsToken) return null;
	const target = e.left;
	if (ts.isElementAccessExpression(target) && ts.isIdentifier(target.expression) && ts.isStringLiteral(target.argumentExpression)) {
		return { name: target.expression.text, key: target.argumentExpression.text, value: e.right };
	}
	if (ts.isPropertyAccessExpression(target) && ts.isIdentifier(target.expression)) {
		return { name: target.expression.text, key: target.name.text, value: e.right };
	}
	return null;
}

export function placeholders(text) {
	return [...new Set([...text.matchAll(/\{(\w+)\}/g)].map((m) => m[1]))].sort();
}

/**
 * Builds the desktop catalog: every web key (i18n.svelte.ts `pt` and `en`), the mission texts
 * (missions.ts `texts` -> mission.<id>.<field>) and the desktop-only keys (desktop.json).
 * Returns { rows: [[key, pt, en]], problems: [string] }.
 */
export function buildCatalog(ts, paths) {
	const problems = [];
	const i18n = readConsts(ts, paths.webI18n);
	const pt = i18n.get('pt');
	const en = i18n.get('en');
	const missions = readConsts(ts, paths.webMissions);
	const texts = missions.get('texts');
	problems.push(...i18n.duplicates, ...missions.duplicates);

	const rows = new Map();
	const add = (key, ptText, enText, origin) => {
		if (rows.has(key)) problems.push(`${origin}: key '${key}' already comes from ${rows.get(key).origin}`);
		else rows.set(key, { pt: ptText, en: enText, origin });
	};
	for (const key of Object.keys(pt)) add(key, pt[key], en[key] ?? '', 'web i18n');
	for (const key of Object.keys(en)) if (!Object.hasOwn(pt, key)) problems.push(`web i18n: '${key}' has no pt-BR text`);
	for (const [id, byLocale] of Object.entries(texts)) {
		for (const field of MISSION_FIELDS) {
			add(`mission.${id}.${field}`, byLocale['pt-BR']?.[field] ?? '', byLocale.en?.[field] ?? '', 'web missions');
		}
	}
	const game = gameRows(ts, paths);
	problems.push(...game.problems);
	for (const [key, ptText, enText, origin] of game.rows) add(key, ptText, enText, origin);
	const desktop = JSON.parse(readFileSync(paths.desktopJson, 'utf8'));
	for (const [key, byLocale] of Object.entries(desktop)) {
		if (rows.has(key)) problems.push(`desktop.json: key '${key}' duplicates a web key`);
		else add(key, byLocale['pt-BR'] ?? '', byLocale.en ?? '', 'desktop.json');
	}

	for (const [key, row] of rows) {
		if (!row.pt || !row.en) problems.push(`${row.origin}: '${key}' is missing a language (${LOCALES.join(', ')})`);
		const a = placeholders(row.pt).join(',');
		const b = placeholders(row.en).join(',');
		if (a !== b) problems.push(`${row.origin}: '${key}' placeholders differ: pt-BR {${a}} vs en {${b}}`);
	}
	return { rows: [...rows].map(([key, r]) => [key, r.pt, r.en]), problems };
}

function csvField(text) {
	return /[",\r\n]|^\s|\s$|^#/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

export function toCsv(rows) {
	const lines = ['# Generated by apps/desktop/scripts/gen_i18n.mjs from apps/web i18n. Do not edit.', 'key,pt-BR,en'];
	for (const row of rows) lines.push(row.map(csvField).join(','));
	return lines.join('\n') + '\n';
}

/** The web's pinned publisher keys (packs.ts PINNED_PUBLISHER_KEYS) as JSON text. */
export function pinnedKeysJson(ts, paths) {
	const keys = readConsts(ts, paths.webPacks).get('PINNED_PUBLISHER_KEYS');
	return JSON.stringify(keys, null, 2) + '\n';
}

export function generate(ts, paths) {
	const { rows, problems } = buildCatalog(ts, paths);
	return { csv: toCsv(rows), pinnedKeys: pinnedKeysJson(ts, paths), problems };
}
