// node --test apps/desktop/tests/scripts/screen_sources.test.mjs  (needs apps/web/node_modules)
// The screen texts and data generated from the web sources outside i18n.svelte.ts: the real sources
// produce the expected keys and content, and a stale content.json or a changed source is reported.

import assert from 'node:assert/strict';
import { copyFileSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { checkI18n } from '../../scripts/check_i18n.mjs';
import { DEFAULT_PATHS, generate, loadTypeScript } from '../../scripts/i18n_lib.mjs';

const ts = loadTypeScript();

// The real catalogs, with the outputs (and one source, to edit) copied to a temp folder.
function copy() {
	const dir = mkdtempSync(path.join(tmpdir(), 'tl-screens-'));
	const paths = { ...DEFAULT_PATHS };
	for (const key of ['csv', 'pinnedKeys', 'content', 'webObjectives']) {
		paths[key] = path.join(dir, path.basename(DEFAULT_PATHS[key]));
		copyFileSync(DEFAULT_PATHS[key], paths[key]);
	}
	return paths;
}

test('the real sources give objectives, findings, news names, Academy texts and the content file', () => {
	const { csv, content, problems } = generate(ts, DEFAULT_PATHS);
	assert.deepEqual(problems, []);
	assert.match(csv, /^objective\.risk_per_idea,Use stop: risco planejado de até \{pct\} do patrimônio por ideia\.,/m);
	assert.match(csv, /^finding\.no_stop\.one,"?1 entrada sem stop/m);
	assert.match(csv, /^finding\.stop_beyond_liq\.safe,/m);
	assert.match(csv, /^nextLesson\.risk,/m);
	assert.match(csv, /^newsLevel\.4,muito positivo,very positive$/m);
	assert.match(csv, /^academyLesson\.foundations-bar\.known\.choice\.close6,O fechamento da barra 6,/m);
	const data = JSON.parse(content);
	assert.equal(data.stages.length, 13);
	assert.equal(data.lessons[0].steps.length, 8);
	assert.equal(data.defaultKeymap.advance, 'Space');
	assert.equal(data.campaignCapital, '10000');
});

test('in sync: the committed outputs match', () => {
	assert.deepEqual(checkI18n(ts, copy()), []);
});

test('a stale content.json is reported', () => {
	const paths = copy();
	writeFileSync(paths.content, '{}\n');
	assert.ok(checkI18n(ts, paths).some((p) => p.includes('content.json is out of date')));
});

test('a changed web objective text makes the committed catalog stale', () => {
	const paths = copy();
	const src = readFileSync(paths.webObjectives, 'utf8').replace('Termine a missão.', 'Termine a missão agora.');
	writeFileSync(paths.webObjectives, src);
	assert.ok(checkI18n(ts, paths).some((p) => p.includes('strings.csv is out of date')));
});
