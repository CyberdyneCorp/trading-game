// node --test apps/desktop/tests/scripts/i18n_game.test.mjs  (needs apps/web/node_modules)
// The terminal catalogs read outside i18n.svelte.ts (news, patterns, objectives, tutorials, lessons).

import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { gameRows } from '../../scripts/i18n_game.mjs';
import { DEFAULT_PATHS, loadTypeScript } from '../../scripts/i18n_lib.mjs';

const ts = loadTypeScript();

function write(dir, name, text) {
	const file = path.join(dir, name);
	writeFileSync(file, text);
	return file;
}

test('reads helper calls, numbers, null and reused elements', () => {
	const dir = mkdtempSync(path.join(tmpdir(), 'tl-game-'));
	const paths = {
		webLessons: write(
			dir,
			'lessons.ts',
			`const q = (pt, en) => ({ 'pt-BR': pt, en });
const TEXTS = { m1: { a: q({ prompt: 'P?', choices: ['x', 'y'] }, { prompt: 'Q?', choices: ['X', 'Y'] }), b: q({ prompt: 'N', choices: [], unit: 'cr' }, { prompt: 'N', choices: [], unit: 'cr' }) } };`
		),
		webTutorials: write(
			dir,
			'tutorials.ts',
			`const m01 = { 'pt-BR': [{ art: 'chart', title: 'T', body: 'B' }], en: [{ art: 'chart', title: 'Te', body: 'Be' }] };
const m02 = { 'pt-BR': [m01['pt-BR'][0]], en: [m01.en[0]] };
const TUTORIALS = { 'm01-a': m01, 'm02-b': m02 };`
		),
		webPatterns: write(
			dir,
			'candlePatterns.ts',
			`const CANDLE_PATTERNS = [{ id: 'hammer', bias: 'bullish', context: null, candles: [[40, 46, 8, -45]], text: { 'pt-BR': { name: 'Martelo', what: 'w', means: 'm', confirm: 'c' }, en: { name: 'Hammer', what: 'w', means: 'm', confirm: 'c' } } }];
const MORE_CANDLE_PATTERNS = [];`
		)
	};
	const { rows, problems } = gameRows(ts, paths);
	assert.deepEqual(problems, []);
	const byKey = Object.fromEntries(rows.map(([k, pt, en]) => [k, [pt, en]]));
	assert.deepEqual(byKey['lessonq.m1.a.choice1'], ['y', 'Y']);
	assert.deepEqual(byKey['lessonq.m1.b.unit'], ['cr', 'cr']);
	assert.equal(byKey['lessonq.m1.a.unit'], undefined);
	assert.deepEqual(byKey['tutorial.m02-b.0.title'], ['T', 'Te']);
	assert.deepEqual(byKey['pattern.hammer.name'], ['Martelo', 'Hammer']);
});

test('the real web catalogs give every terminal key in both languages', () => {
	const { rows, problems } = gameRows(ts, DEFAULT_PATHS);
	assert.deepEqual(problems, []);
	const keys = new Set(rows.map((r) => r[0]));
	for (const key of ['news.headline.rate_decision.neg', 'news.kind.macro_data', 'news.level.4', 'objective.risk_per_idea', 'tutorial.m01-false-breakout.0.title', 'lessonq.m01-false-breakout.breakout-plan.choice2']) {
		assert.ok(keys.has(key), key);
	}
	for (const [key, pt, en] of rows) assert.ok(pt && en, `${key} has both languages`);
});
