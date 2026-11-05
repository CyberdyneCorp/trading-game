#!/usr/bin/env node
// CI check: the committed desktop catalogs must equal what gen_i18n.mjs would write now, no desktop-only
// key may duplicate a web key, no catalog may repeat a key, and every key must have the same {placeholders}
// in pt-BR and en.   node apps/desktop/scripts/check_i18n.mjs

import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { DEFAULT_PATHS, generate, loadTypeScript } from './i18n_lib.mjs';

/** Returns the list of problems (empty when the catalogs are in sync and consistent). */
export function checkI18n(ts, paths) {
	const { csv, pinnedKeys, problems } = generate(ts, paths);
	const stale = (file, expected) => !existsSync(file) || readFileSync(file, 'utf8') !== expected;
	if (stale(paths.csv, csv)) problems.push(`${paths.csv} is out of date: run node apps/desktop/scripts/gen_i18n.mjs`);
	if (stale(paths.pinnedKeys, pinnedKeys)) {
		problems.push(`${paths.pinnedKeys} is out of date: run node apps/desktop/scripts/gen_i18n.mjs`);
	}
	return problems;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
	const problems = checkI18n(loadTypeScript(), DEFAULT_PATHS);
	for (const p of problems) console.error(`i18n: ${p}`);
	if (problems.length) process.exit(1);
	console.log('i18n: desktop catalogs in sync with apps/web');
}
