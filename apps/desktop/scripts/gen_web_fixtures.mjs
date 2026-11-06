#!/usr/bin/env node
// Web parity fixtures (design "Web helpers", tasks 1.8): runs the web's pure helpers over fixed input
// grids and writes what they return to apps/desktop/tests/fixtures/web/<helper>.json. The desktop's
// C++ ports replay every case (tests/viewmodel/test_web_parity.cpp). The helpers are loaded through the
// web's own Vite config (Svelte runes in i18n.svelte.ts included), so they run exactly as in the app.
//
//   node apps/desktop/scripts/gen_web_fixtures.mjs           writes the fixtures
//   node apps/desktop/scripts/gen_web_fixtures.mjs --check   fails when a written fixture differs (CI)
//
// Needs `npm ci` in apps/web. NaN, Infinity and undefined are written as null.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildFixtures } from './web_fixtures_lib.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const web = resolve(here, '../../web');
const out = resolve(here, '../tests/fixtures/web');
const check = process.argv.includes('--check');

const require = createRequire(join(web, 'package.json'));
const { createServer } = await import(require.resolve('vite'));
const server = await createServer({
	root: web,
	configFile: join(web, 'vite.config.ts'),
	server: { middlewareMode: true, hmr: false, ws: false },
	appType: 'custom',
	logLevel: 'error',
	optimizeDeps: { noDiscovery: true, include: [] }
});

const load = (name) => server.ssrLoadModule(`/src/lib/game/${name}`);
// The engine's what-if golden fixtures: real engine answers for the lab's chart geometry.
const golden = (name) => JSON.parse(readFileSync(resolve(here, `../../../engine/tests/golden/${name}.json`), 'utf8')).expected.result;
let fixtures;
try {
	fixtures = await buildFixtures(load, golden);
} finally {
	await server.close();
}

const plain = (v) => JSON.stringify(v, (_k, x) => (typeof x === 'number' && !Number.isFinite(x) ? null : x));
// One case per line: a drift shows as the lines of the cases that changed.
const text = ({ helper, cases }) => `{"helper":${plain(helper)},"cases":[\n${cases.map(plain).join(',\n')}\n]}\n`;
let drift = 0;
mkdirSync(out, { recursive: true });
for (const [name, value] of Object.entries(fixtures)) {
	const file = join(out, `${name}.json`);
	const body = text(value);
	if (check) {
		const old = existsSync(file) ? readFileSync(file, 'utf8') : '';
		if (old !== body) {
			console.error(`drift: tests/fixtures/web/${name}.json differs from what the web helpers return now`);
			drift++;
		}
	} else {
		writeFileSync(file, body);
	}
}
if (drift) {
	console.error('run: node apps/desktop/scripts/gen_web_fixtures.mjs, then make the C++ ports pass the new cases');
	process.exit(1);
}
console.log(`${check ? 'checked' : 'wrote'} ${Object.keys(fixtures).length} web fixture files`);
