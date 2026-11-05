#!/usr/bin/env node
// Fails when the engine module (single-file ES module with the wasm embedded) is over its
// gzip size budget (spec deterministic-engine "WASM size budget": < 400 KB gzipped).
// Usage: node scripts/check-wasm-size.mjs [path] [budgetBytes]
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';

const DEFAULT_PATH = fileURLToPath(new URL('../src/lib/engine/wasm/tradelab.mjs', import.meta.url));
const DEFAULT_BUDGET = 400 * 1024;

const path = process.argv[2] ?? DEFAULT_PATH;
const budget = Number(process.argv[3] ?? DEFAULT_BUDGET);
const raw = readFileSync(path);
const gz = gzipSync(raw, { level: 9 }).length;
const kb = (n) => `${(n / 1024).toFixed(1)} KB`;
const line = `engine module: ${kb(raw.length)} raw, ${kb(gz)} gzip (budget ${kb(budget)})`;
if (gz >= budget) {
	console.error(`${line}: OVER BUDGET`);
	process.exit(1);
}
console.log(line);
