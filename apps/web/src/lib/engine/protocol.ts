// Typed request/response protocol between the UI thread and the engine worker.
// Responses carry only current-step state: the worker never sends future bars.

import type {
	CommandResult,
	CsvMeta,
	CsvReport,
	DebriefData,
	GameState,
	MissionInfo,
	OrderSideName,
	OrderTypeName,
	PackCheck,
	RunOptionsView,
	TrustedKey
} from './types';

export type EngineRequest =
	| { kind: 'init' }
	| { kind: 'start'; missionId: string; seed: number; options?: RunOptionsView }
	// Packs and CSV import: bytes cross the worker boundary, prices only as tl_state reveals them.
	| { kind: 'deviceKey'; seedHex: string }
	| { kind: 'csvValidate'; csv: string; meta: CsvMeta }
	| { kind: 'csvBuild'; csv: string; meta: CsvMeta; seedHex: string }
	| { kind: 'packValidate'; pack: Uint8Array; keys: TrustedKey[] }
	| { kind: 'startPack'; pack: Uint8Array; keys: TrustedKey[]; seed: number }
	| {
			kind: 'order';
			clientId: number;
			type: OrderTypeName;
			side: OrderSideName;
			qty: string;
			price: string | null;
			reduceOnly: boolean;
			attachStop: string | null;
			attachTp: string | null;
			trailDistance: string | null;
	  }
	| {
			kind: 'option';
			clientId: number;
			series: number;
			side: OrderSideName;
			type: 'market' | 'limit';
			qty: string;
			premium: string | null;
	  }
	| { kind: 'margin'; clientId: number; amount: string }
	| {
			kind: 'plan';
			clientId: number;
			side: OrderSideName;
			skipped: boolean;
			option: boolean;
			loseScenario: boolean;
			invalidation: string | null;
			target: string | null;
			confidence: number;
			plannedRisk: string | null;
	  }
	| { kind: 'answer'; clientId: number; pBp: number; skipped: boolean }
	| { kind: 'cancel'; clientId: number; orderId: number }
	| { kind: 'leverage'; clientId: number; leverage: number }
	| { kind: 'close'; clientId: number }
	| { kind: 'advice'; clientId: number }
	| { kind: 'advance'; steps: number }
	| { kind: 'debrief' };

export interface CommandResponse {
	result: CommandResult;
	state: GameState;
}

/** tl_pack_build_csv result plus the pack bytes (empty when not ok). */
export interface CsvBuildResult {
	ok: boolean;
	reason: string;
	report: CsvReport;
	scenarioId: string;
	digest: string;
	size: number;
	pack: Uint8Array;
}

interface ResponseMap {
	init: { version: { engine: string; executionPolicy: number }; missions: MissionInfo[] };
	deviceKey: TrustedKey;
	csvValidate: CsvReport;
	csvBuild: CsvBuildResult;
	packValidate: PackCheck;
	start: GameState;
	startPack: GameState;
	advance: GameState;
	debrief: DebriefData;
}

export type EngineResponse<K extends EngineRequest['kind']> = K extends keyof ResponseMap ? ResponseMap[K] : CommandResponse;

export interface WorkerEnvelope {
	id: number;
	req: EngineRequest;
}

export type WorkerReply = { id: number; ok: true; res: unknown } | { id: number; ok: false; error: string };
