/// <reference lib="webworker" />
// Hosts the WASM engine off the main thread. Scenario data (including future bars) lives only
// here; replies contain the state up to the current decision step.

import type { TradelabModule } from './wasm/tradelab.mjs';
import type { CsvBuildResult, EngineRequest, WorkerEnvelope, WorkerReply } from './protocol';
import { revealAllowed } from './reveal';
import { abiError, ENGINE_ABI, moduleAbi } from './abi';
import { DEFAULT_RUN_OPTIONS } from './defaults';
import { ORDER_TYPE_CODE, SIDE_CODE, type GameState, type TimeInForceName, type TrustedKey } from './types';

type Fn = (...a: unknown[]) => number;

interface Api {
	version: Fn;
	missions: Fn;
	create: Fn;
	destroy: Fn;
	place: Fn;
	trailRate: Fn;
	option: Fn;
	cancel: Fn;
	move: Fn;
	margin: Fn;
	plan: Fn;
	answer: Fn;
	lessonAnswer: Fn;
	lesson: Fn;
	leverage: Fn;
	close: Fn;
	advice: Fn;
	advance: Fn;
	state: Fn;
	debrief: Fn;
	reveal: Fn;
	deviceKey: Fn;
	csvValidate: Fn;
	csvBuild: Fn;
	blobData: Fn;
	blobSize: Fn;
	packValidate: Fn;
	createPack: Fn;
	checkpoint: Fn;
	restore: Fn;
	restorePack: Fn;
	// Option analysis (add-academy-and-strategies D4); null on builds that predate it.
	whatif: Fn | null;
	runWhatif: Fn | null;
	comboPreview: Fn | null;
	// Run files (add-run-sharing-and-journal-stats D2); null on builds that predate them.
	verifyRun: Fn | null;
	sealRun: Fn | null;
	// Policy v18 (add-practice-and-advanced-strategies D6, D7); null on builds that predate it.
	placeTif: Fn | null;
	optionTif: Fn | null;
	linkOco: Fn | null;
}

const TIF_CODE: Record<TimeInForceName, number> = { gtc: 0, bars: 1, ioc: 2, fok: 3 };

let mod: TradelabModule | null = null;
let api: Api | null = null;
// The engine build in use (an absolute module URL): this version's, or the one an unfinished run
// started with (engine pinning). Set by 'init' and 'useEngine'.
let engineUrl = '';
let run = 0;
// Mission and seed of the current run, for its reveal after the end (null on an imported pack).
let current: { missionId: string; seed: number } | null = null;

async function load(): Promise<Api> {
	if (api) return api;
	if (!engineUrl) throw new Error('engine build not set');
	const factory = ((await import(/* @vite-ignore */ engineUrl)) as { default: () => Promise<TradelabModule> }).default;
	const loaded = await factory();
	// A build with another C ABI is never driven (a pinned older build: its run cannot resume here).
	const abi = moduleAbi(loaded);
	if (abi !== ENGINE_ABI) throw abiError(abi);
	mod = loaded;
	const w = mod.cwrap.bind(mod);
	api = {
		version: w('tl_version', 'number', []),
		missions: w('tl_missions', 'number', []),
		create: w('tl_run_create_ex', 'number', ['string', 'number', 'number', 'number', 'number', 'string', 'number']),
		destroy: w('tl_run_destroy', null, ['number']),
		place: w('tl_place_order_ex', 'number', [
			'number', 'number', 'number', 'number', 'string', 'string', 'number', 'string', 'string', 'string'
		]),
		trailRate: w('tl_place_trailing_rate', 'number', ['number', 'number', 'number', 'string', 'number', 'string']),
		option: w('tl_place_option_order', 'number', ['number', 'number', 'number', 'number', 'number', 'string', 'string']),
		cancel: w('tl_cancel_order', 'number', ['number', 'number', 'number']),
		move: w('tl_move_order', 'number', ['number', 'number', 'number', 'string']),
		margin: w('tl_adjust_margin', 'number', ['number', 'number', 'string']),
		plan: w('tl_record_plan', 'number', [
			'number', 'number', 'number', 'number', 'number', 'number', 'string', 'string', 'number', 'string'
		]),
		answer: w('tl_answer_question', 'number', ['number', 'number', 'number', 'number']),
		lessonAnswer: w('tl_answer_lesson', 'number', ['number', 'number', 'number', 'number', 'string', 'number']),
		lesson: w('tl_lesson', 'number', ['string']),
		leverage: w('tl_set_leverage', 'number', ['number', 'number', 'number']),
		close: w('tl_close_position', 'number', ['number', 'number']),
		advice: w('tl_request_advice', 'number', ['number', 'number']),
		advance: w('tl_advance', 'number', ['number', 'number']),
		state: w('tl_state', 'number', ['number']),
		debrief: w('tl_debrief', 'number', ['number']),
		reveal: w('tl_mission_reveal', 'number', ['string', 'number']),
		deviceKey: w('tl_device_key', 'number', ['string']),
		// The CSV goes in by pointer (withText): a 'string' argument is copied onto the 64 KB stack.
		csvValidate: w('tl_csv_validate', 'number', ['number', 'string']),
		csvBuild: w('tl_pack_build_csv', 'number', ['number', 'string', 'string']),
		blobData: w('tl_blob_data', 'number', []),
		blobSize: w('tl_blob_size', 'number', []),
		packValidate: w('tl_pack_validate', 'number', ['number', 'number', 'string', 'number']),
		createPack: w('tl_run_create_pack', 'number', ['number', 'number', 'string', 'number']),
		checkpoint: w('tl_checkpoint', 'number', ['number']),
		// Checkpoint hex by pointer (withText), like the CSV: it grows past the stack with long runs.
		restore: w('tl_run_restore', 'number', ['string', 'number', 'number']),
		restorePack: w('tl_run_restore_pack', 'number', ['number', 'number', 'string', 'number', 'number']),
		// Feature-detected: a run pinned to an older build has no analysis, and the client hides it.
		whatif: exported(mod, 'tl_option_whatif') ? w('tl_option_whatif', 'number', ['number']) : null,
		runWhatif: exported(mod, 'tl_run_option_whatif') ? w('tl_run_option_whatif', 'number', ['number', 'number']) : null,
		comboPreview: exported(mod, 'tl_combo_preview') ? w('tl_combo_preview', 'number', ['number', 'number']) : null,
		verifyRun: exported(mod, 'tl_run_file_verify') ? w('tl_run_file_verify', 'number', ['number', 'number', 'number', 'string']) : null,
		sealRun: exported(mod, 'tl_run_file_seal') ? w('tl_run_file_seal', 'number', ['number', 'number', 'number', 'string']) : null,
		placeTif: exported(mod, 'tl_place_order_tif')
			? w('tl_place_order_tif', 'number', [
					'number', 'number', 'number', 'number', 'string', 'string', 'number', 'string', 'string', 'string', 'string', 'number', 'number'
				])
			: null,
		optionTif: exported(mod, 'tl_place_option_order_tif')
			? w('tl_place_option_order_tif', 'number', ['number', 'number', 'number', 'number', 'number', 'string', 'string', 'number', 'number'])
			: null,
		linkOco: exported(mod, 'tl_link_oco') ? w('tl_link_oco', 'number', ['number', 'number', 'number', 'number']) : null
	};
	return api;
}

function exported(m: TradelabModule, name: string): boolean {
	return typeof (m as unknown as Record<string, unknown>)[`_${name}`] === 'function';
}

// A trap leaves the module's memory in an unknown state: drop it (and the run it held) so the next
// request loads a fresh instance instead of failing until the page is reloaded.
function resetEngine(): void {
	mod = null;
	api = null;
	run = 0;
	current = null;
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
	current = null;
	run = withBytes(pack, (ptr, size) => a.createPack(ptr, size, JSON.stringify(keys), seed >>> 0));
	if (!run) throw new Error('pack failed validation');
	return json(a.state(run)) as GameState;
}

// Switches to another engine build (a new module instance); the run held by the old one is dropped.
async function useEngine(url: string): Promise<{ engine: string; executionPolicy: number }> {
	if (url !== engineUrl) {
		resetEngine();
		engineUrl = url;
	}
	const a = await load();
	return json(a.version()) as { engine: string; executionPolicy: number };
}

// Restores a checkpoint (the hex goes through the heap: it grows with the run's event log).
// Null when the engine refuses it (another format or scenario, damaged): the caller replays instead.
function restore(a: Api, req: Extract<EngineRequest, { kind: 'restore' }>): GameState | null {
	if (run) a.destroy(run);
	run = 0;
	current = null;
	const seed = req.seed >>> 0;
	if (req.pack) {
		const keys = JSON.stringify(req.keys ?? []);
		run = withBytes(req.pack, (ptr, size) => withText(req.checkpoint, (hex) => a.restorePack(ptr, size, keys, seed, hex)));
	} else {
		run = withText(req.checkpoint, (hex) => a.restore(req.missionId, seed, hex));
		if (run) current = { missionId: req.missionId, seed };
	}
	return run ? (json(a.state(run)) as GameState) : null;
}

function requireRun(): number {
	if (!run) throw new Error('no active run');
	return run;
}

// The reveal describes the run's future: it crosses the worker boundary only after the run ended.
function reveal(a: Api): unknown {
	const status = (json(a.state(requireRun())) as GameState).status;
	if (!revealAllowed(status)) throw new Error('run_active');
	return current ? json(a.reveal(current.missionId, current.seed)) : null;
}

// The mission's lesson questions (no answer key crosses: the engine keeps it until the debrief).
function lessonInfo(a: Api, missionId: string): unknown {
	const info = json(a.lesson(missionId)) as { error?: string };
	return info.error ? null : info;
}

type AnalysisRequest = Extract<EngineRequest, { kind: 'whatif' | 'runWhatif' | 'comboPreview' }>;

// Report-only option analysis: the request JSON goes through the heap (withText), the reply is the
// engine's object or its error code. Nothing is logged and the run is not changed.
function analysis(a: Api, req: AnalysisRequest): { ok: true; value: unknown } | { ok: false; error: string } {
	const fn = a[req.kind];
	if (!fn) return { ok: false, error: 'unsupported' };
	const text = JSON.stringify(req.request);
	const out = withText(text, (ptr) => json(req.kind === 'whatif' ? fn(ptr) : fn(requireRun(), ptr))) as { error?: string };
	return out.error ? { ok: false, error: out.error } : { ok: true, value: out };
}

type RunFileRequest = Extract<EngineRequest, { kind: 'verifyRun' | 'sealRun' }>;

// Run-less: the file text and the pack go through the heap; the run held by the worker is untouched.
function runFile(a: Api, req: RunFileRequest): unknown {
	const fn = a[req.kind];
	if (!fn) return { ok: false, reason: 'unsupported', pack: '' };
	const keys = JSON.stringify(req.keys ?? []);
	const call = (ptr: number, size: number) => withText(req.text, (text) => json(fn(text, ptr, size, keys)));
	return req.pack?.length ? withBytes(req.pack, call) : call(0, 0);
}

type OrderRequest = Extract<EngineRequest, { kind: 'order' }>;

// The policy v18 call for an order with a time in force; an older build has none (the UI hides the
// control there, so reaching this is a bug, not a silent gtc).
function tifCall(fn: Fn | null): Fn {
	if (!fn) throw new Error('unsupported');
	return fn;
}

// A trailing stop given as a rate of the close has its own entry point (policy v16); a time in force
// goes through tl_place_order_tif (policy v18). Without one, the pre-v18 calls: older builds run them.
function placeOrder(a: Api, req: OrderRequest): number {
	const side = SIDE_CODE[req.side];
	const ro = req.reduceOnly ? 1 : 0;
	const t = ORDER_TYPE_CODE[req.type];
	if (req.tif) {
		const rate = req.type === 'trailing_stop' ? (req.trailRate ?? '') : '';
		return tifCall(a.placeTif)(requireRun(), req.clientId, t, side, req.qty, req.price ?? '', ro, req.attachStop ?? '', req.attachTp ?? '',
			req.trailDistance ?? '', rate, TIF_CODE[req.tif], req.tifBars ?? 0);
	}
	if (req.type === 'trailing_stop' && req.trailRate) return a.trailRate(requireRun(), req.clientId, side, req.qty, ro, req.trailRate);
	return a.place(requireRun(), req.clientId, t, side, req.qty, req.price ?? '', ro, req.attachStop ?? '', req.attachTp ?? '', req.trailDistance ?? '');
}

function withState(a: Api, resultPtr: number) {
	const result = json(resultPtr);
	return { result, state: json(a.state(requireRun())) };
}

function start(a: Api, req: Extract<EngineRequest, { kind: 'start' }>): GameState {
	if (run) a.destroy(run);
	const o = req.options ?? DEFAULT_RUN_OPTIONS;
	run = a.create(req.missionId, req.seed >>> 0, o.adviceAllowance, o.newsLead, o.calendar ? 1 : 0, o.startingBalance ?? '', o.leverageCap ?? 0);
	if (!run) throw new Error(`unknown mission ${req.missionId} or invalid start options`);
	current = { missionId: req.missionId, seed: req.seed >>> 0 };
	return json(a.state(run)) as GameState;
}

function placeOption(a: Api, req: Extract<EngineRequest, { kind: 'option' }>): number {
	const type = req.type === 'limit' ? 1 : 0;
	if (req.tif) {
		return tifCall(a.optionTif)(requireRun(), req.clientId, req.series, SIDE_CODE[req.side], type, req.qty, req.premium ?? '', TIF_CODE[req.tif], req.tifBars ?? 0);
	}
	return a.option(requireRun(), req.clientId, req.series, SIDE_CODE[req.side], type, req.qty, req.premium ?? '');
}

function recordPlan(a: Api, req: Extract<EngineRequest, { kind: 'plan' }>): number {
	const flag = (b: boolean) => (b ? 1 : 0);
	return a.plan(
		requireRun(),
		req.clientId,
		SIDE_CODE[req.side],
		flag(req.skipped),
		flag(req.option),
		flag(req.loseScenario),
		req.invalidation ?? '',
		req.target ?? '',
		req.confidence,
		req.plannedRisk ?? ''
	);
}

async function handle(req: EngineRequest): Promise<unknown> {
	if (req.kind === 'init') return { version: await useEngine(req.engineUrl), missions: json(api!.missions()) };
	if (req.kind === 'useEngine') return useEngine(req.engineUrl);
	const a = await load();
	switch (req.kind) {
		case 'start':
			return start(a, req);
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
			return withState(a, placeOrder(a, req));
		case 'option':
			return withState(a, placeOption(a, req));
		case 'plan':
			return withState(a, recordPlan(a, req));
		case 'answer':
			return withState(a, a.answer(requireRun(), req.clientId, req.pBp, req.skipped ? 1 : 0));
		case 'lesson':
			return lessonInfo(a, req.missionId);
		case 'whatif':
		case 'runWhatif':
		case 'comboPreview':
			return analysis(a, req);
		case 'verifyRun':
		case 'sealRun':
			return runFile(a, req);
		case 'lessonAnswer':
			return withState(
				a,
				a.lessonAnswer(requireRun(), req.clientId, req.question, req.choice, req.value ?? '', req.skipped ? 1 : 0)
			);
		case 'margin':
			return withState(a, a.margin(requireRun(), req.clientId, req.amount));
		case 'cancel':
			return withState(a, a.cancel(requireRun(), req.clientId, req.orderId));
		case 'move':
			return withState(a, a.move(requireRun(), req.clientId, req.orderId, req.price));
		case 'oco':
			return withState(a, tifCall(a.linkOco)(requireRun(), req.clientId, req.orderA, req.orderB));
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
		case 'reveal':
			return reveal(a);
		case 'checkpoint':
			return json(a.checkpoint(requireRun()));
		case 'restore':
			return restore(a, req);
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
