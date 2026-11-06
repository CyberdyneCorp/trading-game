// node --test apps/desktop/tests/scripts/  (needs apps/web/node_modules: npm ci in apps/web)
// Each failure mode of check_i18n.mjs on a small failing fixture, plus the passing case.

import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { checkI18n } from '../../scripts/check_i18n.mjs';
import { generate, loadTypeScript } from '../../scripts/i18n_lib.mjs';

const ts = loadTypeScript();

const WEB_I18N = `
const pt = {
	'menu.start': 'Iniciar missão',
	'top.step': 'Barra {n}'
} as const;
const en: Record<string, string> = {
	'menu.start': 'Start mission',
	'top.step': 'Bar {n}'
};
`;
const MISSIONS = `
const texts = {
	'm01-x': { 'pt-BR': { title: 'Missão 01', tagline: 'a', topic: 'b', reveal: 'c' }, en: { title: 'Mission 01', tagline: 'a', topic: 'b', reveal: 'c' } }
};
texts['m02-y'] = { 'pt-BR': { title: 'Missão 02', tagline: 'a', topic: 'b', reveal: 'c' }, en: { title: 'Mission 02', tagline: 'a', topic: 'b', reveal: 'c' } };
`;
const PACKS = `const KEY = { id: 'publisher-1', key: 'ab' };\nexport const PINNED_PUBLISHER_KEYS = [KEY];\n`;
const DESKTOP = { 'desktop.only': { 'pt-BR': 'Só desktop', en: 'Desktop only' } };

function fixture({ webI18n = WEB_I18N, desktop = DESKTOP, csvEdit = null } = {}) {
	const dir = mkdtempSync(path.join(tmpdir(), 'tl-i18n-'));
	const paths = {
		webI18n: path.join(dir, 'i18n.svelte.ts'),
		webMissions: path.join(dir, 'missions.ts'),
		webPacks: path.join(dir, 'packs.ts'),
		desktopJson: path.join(dir, 'desktop.json'),
		csv: path.join(dir, 'strings.csv'),
		pinnedKeys: path.join(dir, 'pinned_keys.json')
	};
	writeFileSync(paths.webI18n, webI18n);
	writeFileSync(paths.webMissions, MISSIONS);
	writeFileSync(paths.webPacks, PACKS);
	writeFileSync(paths.desktopJson, JSON.stringify(desktop));
	const { csv, pinnedKeys } = generate(ts, paths);
	writeFileSync(paths.csv, csvEdit ? csvEdit(csv) : csv);
	writeFileSync(paths.pinnedKeys, pinnedKeys);
	return paths;
}

test('in sync: no problems, missions added by assignment are included', () => {
	const paths = fixture();
	assert.deepEqual(checkI18n(ts, paths), []);
	const { csv } = generate(ts, paths);
	assert.match(csv, /^mission\.m02-y\.title,Missão 02,Mission 02$/m);
	assert.match(csv, /^desktop\.only,Só desktop,Desktop only$/m);
});

test('drift: a web string changed without regenerating the desktop catalog', () => {
	const paths = fixture({ csvEdit: (csv) => csv.replace('Iniciar missão', 'Começar') });
	const problems = checkI18n(ts, paths);
	assert.equal(problems.length, 1);
	assert.match(problems[0], /out of date/);
});

test('duplicate: a desktop-only key that repeats a web key', () => {
	const paths = fixture({ desktop: { 'menu.start': { 'pt-BR': 'x', en: 'y' } } });
	assert.ok(checkI18n(ts, paths).some((p) => /duplicates a web key/.test(p)));
});

test('duplicate: a key written twice in the web catalog', () => {
	const paths = fixture({ webI18n: WEB_I18N.replace("'top.step': 'Barra {n}'", "'top.step': 'Barra {n}',\n\t'menu.start': 'De novo'") });
	assert.ok(checkI18n(ts, paths).some((p) => /duplicate key 'menu.start'/.test(p)));
});

test('placeholders: pt-BR and en must name the same {placeholders}', () => {
	const paths = fixture({ webI18n: WEB_I18N.replace("'Bar {n}'", "'Bar {count}'") });
	assert.ok(checkI18n(ts, paths).some((p) => /'top.step' placeholders differ/.test(p)));
});

test('computed entries are refused instead of silently dropped', () => {
	assert.throws(() => fixture({ webI18n: WEB_I18N.replace("'Start mission'", 'String(1)') }), /not a data literal/);
});
