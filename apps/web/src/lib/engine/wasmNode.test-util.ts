// The browser WASM engine (scripts/build-engine.sh) loaded in Vitest's Node environment, so unit tests can
// check the client's requests against the engine kernel itself. The module is built for web and worker
// only; a worker-like global is enough for it to start in Node.

import type { TradelabModule } from './wasm/tradelab.mjs';

// Empty when the build is missing (CI builds it before the unit tests).
const modules = import.meta.glob<{ default: () => Promise<TradelabModule> }>('./wasm/tradelab.mjs');
const load = modules['./wasm/tradelab.mjs'];

/** False when the WASM build is missing. */
export const wasmBuilt = load != null;

export interface NodeEngine {
	call<T>(fn: string, args: (string | number)[], types: ('string' | 'number')[]): T;
	whatif(request: unknown): unknown;
	createRun(mission: string, seed: number): number;
	runWhatif(run: number, request: unknown): unknown;
	comboPreview(run: number, request: unknown): unknown;
	state(run: number): unknown;
}

export async function loadEngine(): Promise<NodeEngine> {
	const g = globalThis as Record<string, unknown>;
	g.self ??= globalThis;
	g.importScripts ??= () => {};
	const mod = await (await load()).default();
	const call = <T>(fn: string, args: (string | number)[], types: ('string' | 'number')[]): T =>
		JSON.parse(mod.UTF8ToString(mod.cwrap(fn, 'number', types)(...args))) as T;
	return {
		call,
		whatif: (r) => call('tl_option_whatif', [JSON.stringify(r)], ['string']),
		createRun: (mission, seed) => mod.cwrap('tl_run_create', 'number', ['string', 'number'])(mission, seed),
		runWhatif: (run, r) => call('tl_run_option_whatif', [run, JSON.stringify(r)], ['number', 'string']),
		comboPreview: (run, r) => call('tl_combo_preview', [run, JSON.stringify(r)], ['number', 'string']),
		state: (run) => call('tl_state', [run], ['number'])
	};
}
