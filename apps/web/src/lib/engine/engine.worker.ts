/// <reference lib="webworker" />
// Hosts the WASM engine off the main thread. Scenario data (including future bars) lives only
// here; replies contain the state up to the current decision step.

import createTradelab, { type TradelabModule } from './wasm/tradelab.mjs';
import type { EngineRequest, WorkerEnvelope, WorkerReply } from './protocol';
import { ORDER_TYPE_CODE, SIDE_CODE } from './types';

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
	leverage: Fn;
	close: Fn;
	advance: Fn;
	state: Fn;
	debrief: Fn;
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
		create: w('tl_run_create', 'number', ['string', 'number']),
		destroy: w('tl_run_destroy', null, ['number']),
		place: w('tl_place_order_ex', 'number', [
			'number', 'number', 'number', 'number', 'string', 'string', 'number', 'string', 'string', 'string'
		]),
		option: w('tl_place_option_order', 'number', ['number', 'number', 'number', 'number', 'number', 'string', 'string']),
		cancel: w('tl_cancel_order', 'number', ['number', 'number', 'number']),
		margin: w('tl_adjust_margin', 'number', ['number', 'number', 'string']),
		leverage: w('tl_set_leverage', 'number', ['number', 'number', 'number']),
		close: w('tl_close_position', 'number', ['number', 'number']),
		advance: w('tl_advance', 'number', ['number', 'number']),
		state: w('tl_state', 'number', ['number']),
		debrief: w('tl_debrief', 'number', ['number'])
	};
	return api;
}

function json(ptr: number): unknown {
	if (!mod) throw new Error('engine not loaded');
	return JSON.parse(mod.UTF8ToString(ptr));
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
		case 'start':
			if (run) a.destroy(run);
			run = a.create(req.missionId, req.seed >>> 0);
			if (!run) throw new Error(`unknown mission ${req.missionId}`);
			return json(a.state(run));
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
		case 'margin':
			return withState(a, a.margin(requireRun(), req.clientId, req.amount));
		case 'cancel':
			return withState(a, a.cancel(requireRun(), req.clientId, req.orderId));
		case 'leverage':
			return withState(a, a.leverage(requireRun(), req.clientId, req.leverage));
		case 'close':
			return withState(a, a.close(requireRun(), req.clientId));
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
		reply = { id, ok: false, error: err instanceof Error ? err.message : String(err) };
	}
	(self as unknown as DedicatedWorkerGlobalScope).postMessage(reply);
};
