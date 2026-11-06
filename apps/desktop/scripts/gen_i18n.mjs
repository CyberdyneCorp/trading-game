#!/usr/bin/env node
// Regenerates apps/desktop/resources/i18n/strings.csv, resources/packs/pinned_keys.json and
// resources/web/content.json (screen rules as data) from the web client's sources. Run after any change to
// apps/web i18n, missions.ts, packs.ts, the screen sources of screen_sources.mjs or desktop.json:
//   node apps/desktop/scripts/gen_i18n.mjs
// Fails (exit 1, nothing written) when the catalogs have problems check_i18n.mjs would report.

import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { DEFAULT_PATHS, generate, loadTypeScript } from './i18n_lib.mjs';

const ts = loadTypeScript();
const { csv, pinnedKeys, content, problems } = generate(ts, DEFAULT_PATHS);
if (problems.length) {
	for (const p of problems) console.error(`i18n: ${p}`);
	process.exit(1);
}
for (const [file, text] of [
	[DEFAULT_PATHS.csv, csv],
	[DEFAULT_PATHS.pinnedKeys, pinnedKeys],
	[DEFAULT_PATHS.content, content]
]) {
	mkdirSync(path.dirname(file), { recursive: true });
	writeFileSync(file, text);
	console.log(`wrote ${path.relative(process.cwd(), file)}`);
}
