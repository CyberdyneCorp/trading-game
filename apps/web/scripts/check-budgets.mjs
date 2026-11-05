#!/usr/bin/env node
// Bundle-size budget (spec web-client "Performance budgets"): the JavaScript and WASM a first visit
// loads before the menu is usable — the page's scripts and their static imports, the engine worker
// and the engine module with its embedded WASM — must stay under 1 MB gzipped. Run after
// `npm run build`; exits 1 over budget. Lazy chunks and audio are not part of the initial load.
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join, relative } from 'node:path';
import { gzipSync } from 'node:zlib';

const BUDGET = 1_000_000; // bytes, gzipped
const build = join(import.meta.dirname, '..', 'build');
const html = readFileSync(join(build, 'index.html'), 'utf8');

const seen = new Set();
// The entry modules the shell imports, then every static `import ... from "./x.js"` they pull in.
const queue = [...html.matchAll(/(?:import\(|href=|src=)["']\.?\/?(_app\/[^"']+\.js)["']/g)].map((m) => join(build, m[1]));
while (queue.length) {
	const file = queue.pop();
	if (seen.has(file) || !existsSync(file)) continue;
	seen.add(file);
	const code = readFileSync(file, 'utf8');
	for (const m of code.matchAll(/(?:^|[;}\s])import\s*(?:[\w*{}\s,$]+from\s*)?["'](\.{1,2}\/[^"']+\.js)["']/g)) {
		queue.push(join(file, '..', m[1]));
	}
}
const files = (dir) => (existsSync(dir) ? readdirSync(dir).map((f) => join(dir, f)) : []);
for (const f of files(join(build, '_app/immutable/workers'))) if (f.endsWith('.js')) seen.add(f);
for (const f of files(join(build, '_app/immutable/assets'))) if (/tradelab\.[\w-]+\.mjs$/.test(f)) seen.add(f);

let total = 0;
const rows = [...seen].map((f) => {
	const gz = gzipSync(readFileSync(f), { level: 9 }).length;
	total += gz;
	return [relative(build, f), gz];
});
rows.sort((a, b) => b[1] - a[1]);
for (const [f, gz] of rows) console.log(`${(gz / 1024).toFixed(1).padStart(8)} KB  ${f}`);
const ok = total <= BUDGET;
console.log(`initial JS + WASM: ${(total / 1024).toFixed(1)} KB gzipped (budget ${(BUDGET / 1024).toFixed(0)} KB) ${ok ? 'OK' : 'OVER BUDGET'}`);
if (!rows.some(([f]) => f.includes('tradelab.'))) {
	console.error('engine module not found in the build output');
	process.exit(1);
}
process.exit(ok ? 0 : 1);
