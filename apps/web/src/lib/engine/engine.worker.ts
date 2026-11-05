/// <reference lib="webworker" />
// Hosts the WASM engine off the main thread. Scenario data (including future bars) lives only
// here; replies contain the state up to the current decision step.

import createTradelab, { type TradelabModule } from './wasm/tradelab.mjs';
import type { CsvBuildResult, EngineRequest, WorkerEnvelope, WorkerReply } from './protocol';
import { DEFAULT_RUN_OPTIONS, ORDER_TYPE_CODE, SIDE_CODE, type GameState, type TrustedKey } from './types';

type Fn = (...a: unknown[]) => number;

interface Api {
	version: Fn;
	missions: Fn;
	create: Fn;
	destroy: Fn;
	place: Fn;
	option: Fn;
	cancel: Fn;
	margin: Fn;
	plan: Fn;
	answer: Fn;
	leverage: Fn;
	close: Fn;
	advice: Fn;
	advance: Fn;
	state: Fn;
	debrief: Fn;
	deviceKey: Fn;
	csvValidate: Fn;
	csvBuild: Fn;
	blobData: Fn;
	blobSize: Fn;
	packValidate: Fn;
	createPack: Fn;
}

let mod: TradelabModule | null = null;
let api: Api | null = null;
let run = 0;

async function load(): Promise<Api> {
	if (api) return api;
	mod = await createTradelab();
	const w = mod.cwrap.bind(mod);
	api = {
		version: w('tl_version', 'number', []),
		missions: w('tl_missions', 'number', []),
		create: w('tl_run_create_ex', 'number', ['string', 'number', 'number', 'number', 'number']),
		destroy: w('tl_run_destroy', null, ['number']),
		place: w('tl_place_order_ex', 'number', [
			'number', 'number', 'number', 'number', 'string', 'string', 'number', 'string', 'string', 'string'
		]),
		option: w('tl_place_option_order', 'number', ['number', 'number', 'number', 'number', 'number', 'string', 'string']),
		cancel: w('tl_cancel_order', 'number', ['number', 'number', 'number']),
		margin: w('tl_adjust_margin', 'number', ['number', 'number', 'string']),
		plan: w('tl_record_plan', 'number', [
			'number', 'number', 'number', 'number', 'number', 'number', 'string', 'string', 'number', 'string'
		]),
		answer: w('tl_answer_question', 'number', ['number', 'number', 'number', 'number']),
		leverage: w('tl_set_leverage', 'number', ['number', 'number', 'number']),
		close: w('tl_close_position', 'number', ['number', 'number']),
		advice: w('tl_request_advice', 'number', ['number', 'number']),
		advance: w('tl_advance', 'number', ['number', 'number']),
		state: w('tl_state', 'number', ['number']),
		debrief: w('tl_debrief', 'number', ['number']),
		deviceKey: w('tl_device_key', 'number', ['string']),
		// The CSV goes in by pointer (withText): a 'string' argument is copied onto the 64 KB stack.
		csvValidate: w('tl_csv_validate', 'number', ['number', 'string']),
		csvBuild: w('tl_pack_build_csv', 'number', ['number', 'string', 'string']),
		blobData: w('tl_blob_data', 'number', []),
		blobSize: w('tl_blob_size', 'number', []),
		packValidate: w('tl_pack_validate', 'number', ['number', 'number', 'string', 'number']),
		createPack: w('tl_run_create_pack', 'number', ['number', 'number', 'string', 'number'])
	};
	return api;
}

// A trap leaves the module's memory in an unknown state: drop it (and the run it held) so the next
// request loads a fresh instance instead of failing until the page is reloaded.
function resetEngine(): void {
	mod = null;
	api = null;
	run = 0;
}

function json(ptr: number): unknown {
	if (!mod) throw new Error('engine not loaded');
	return JSON.parse(mod.UTF8ToString(ptr));
}

// Copies bytes into the WASM heap for one call; the engine never keeps the pointer.
function withBytes<T>(bytes: Uint8Array, fn: (ptr: number, size: number) => T): T {
	if (!mod) throw new Error('engine not loaded');
	const ptr = mod._malloc(Math.max(1, bytes.length));
	try {
		mod.HEAPU8.set(bytes, ptr);
		return fn(ptr, bytes.length);
	} finally {
		mod._free(ptr);
	}
}

// A NUL-terminated UTF-8 copy of `text` on the heap, for inputs too large for ccall's stack copy.
function withText<T>(text: string, fn: (ptr: number) => T): T {
	const utf8 = new TextEncoder().encode(text);
	const bytes = new Uint8Array(utf8.length + 1);
	bytes.set(utf8);
	return withBytes(bytes, (ptr) => fn(ptr));
}

function csvValidate(a: Api, csv: string, meta: unknown): unknown {
	return withText(csv, (ptr) => json(a.csvValidate(ptr, JSON.stringify(meta))));
}

function csvBuild(a: Api, csv: string, meta: unknown, seedHex: string): CsvBuildResult {
	const res = withText(csv, (ptr) => json(a.csvBuild(ptr, JSON.stringify(meta), seedHex))) as Omit<CsvBuildResult, 'pack'>;
	const ptr = a.blobData();
	const size = a.blobSize();
	return { ...res, pack: mod!.HEAPU8.slice(ptr, ptr + size) };
}

function deviceKey(a: Api, seedHex: string): TrustedKey {
	const k = json(a.deviceKey(seedHex)) as { id?: string; publicKey?: string; error?: string };
	if (!k.id || !k.publicKey) throw new Error(k.error ?? 'invalid device key');
	return { id: k.id, key: k.publicKey };
}

function startPack(a: Api, pack: Uint8Array, keys: TrustedKey[], seed: number): GameState {
	if (run) a.destroy(run);
	run = withBytes(pack, (ptr, size) => a.createPack(ptr, size, JSON.stringify(keys), seed >>> 0));
	if (!run) throw new Error('pack failed validation');
	return json(a.state(run)) as GameState;
}

function requireRun(): number {
	if (!run) throw new Error('no active run');
	return run;
}

function withState(a: Api, resultPtr: number) {
	const result = json(resultPtr);
	return { result, state: json(a.state(requireRun())) };
}

async function handle(req: EngineRequest): Promise<unknown> {
	const a = await load();
	switch (req.kind) {
		case 'init':
			return { version: json(a.version()), missions: json(a.missions()) };
		case 'start': {
			if (run) a.destroy(run);
			const o = req.options ?? DEFAULT_RUN_OPTIONS;
			run = a.create(req.missionId, req.seed >>> 0, o.adviceAllowance, o.newsLead, o.calendar ? 1 : 0);
			if (!run) throw new Error(`unknown mission ${req.missionId}`);
			return json(a.state(run));
		}
		case 'deviceKey':
			return deviceKey(a, req.seedHex);
		case 'csvValidate':
			return csvValidate(a, req.csv, req.meta);
		case 'csvBuild':
			return csvBuild(a, req.csv, req.meta, req.seedHex);
		case 'packValidate':
			return withBytes(req.pack, (ptr, size) => json(a.packValidate(ptr, size, JSON.stringify(req.keys), 0)));
		case 'startPack':
			return startPack(a, req.pack, req.keys, req.seed);
		case 'order':
			return withState(
				a,
				a.place(
					requireRun(),
					req.clientId,
					ORDER_TYPE_CODE[req.type],
					SIDE_CODE[req.side],
					req.qty,
					req.price ?? '',
					req.reduceOnly ? 1 : 0,
					req.attachStop ?? '',
					req.attachTp ?? '',
					req.trailDistance ?? ''
				)
			);
		case 'option':
			return withState(
				a,
				a.option(
					requireRun(),
					req.clientId,
					req.series,
					SIDE_CODE[req.side],
					req.type === 'limit' ? 1 : 0,
					req.qty,
					req.premium ?? ''
				)
			);
		case 'plan':
			return withState(
				a,
				a.plan(
					requireRun(),
					req.clientId,
					SIDE_CODE[req.side],
					req.skipped ? 1 : 0,
					req.option ? 1 : 0,
					req.loseScenario ? 1 : 0,
					req.invalidation ?? '',
					req.target ?? '',
					req.confidence,
					req.plannedRisk ?? ''
				)
			);
		case 'answer':
			return withState(a, a.answer(requireRun(), req.clientId, req.pBp, req.skipped ? 1 : 0));
		case 'margin':
			return withState(a, a.margin(requireRun(), req.clientId, req.amount));
		case 'cancel':
			return withState(a, a.cancel(requireRun(), req.clientId, req.orderId));
		case 'leverage':
			return withState(a, a.leverage(requireRun(), req.clientId, req.leverage));
		case 'close':
			return withState(a, a.close(requireRun(), req.clientId));
		case 'advice':
			return withState(a, a.advice(requireRun(), req.clientId));
		case 'advance':
			return json(a.advance(requireRun(), req.steps));
		case 'debrief':
			return json(a.debrief(requireRun()));
	}
}

self.onmessage = async (e: MessageEvent<WorkerEnvelope>) => {
	const { id, req } = e.data;
	let reply: WorkerReply;
	try {
		reply = { id, ok: true, res: await handle(req) };
	} catch (err) {
		if (err instanceof WebAssembly.RuntimeError) resetEngine();
		reply = { id, ok: false, error: err instanceof Error ? err.message : String(err) };
	}
	(self as unknown as DedicatedWorkerGlobalScope).postMessage(reply);
};
