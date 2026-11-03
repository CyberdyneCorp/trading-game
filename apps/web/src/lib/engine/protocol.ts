// Typed request/response protocol between the UI thread and the engine worker.
// Responses carry only current-step state: the worker never sends future bars.

import type { CommandResult, DebriefData, GameState, MissionInfo, OrderSideName, OrderTypeName } from './types';

export type EngineRequest =
	| { kind: 'init' }
	| { kind: 'start'; missionId: string; seed: number }
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
	| { kind: 'cancel'; clientId: number; orderId: number }
	| { kind: 'leverage'; clientId: number; leverage: number }
	| { kind: 'close'; clientId: number }
	| { kind: 'advance'; steps: number }
	| { kind: 'debrief' };

export interface CommandResponse {
	result: CommandResult;
	state: GameState;
}

export type EngineResponse<K extends EngineRequest['kind']> = K extends 'init'
	? { version: string; missions: MissionInfo[] }
	: K extends 'start' | 'advance'
		? GameState
		: K extends 'debrief'
			? DebriefData
			: CommandResponse;

export interface WorkerEnvelope {
	id: number;
	req: EngineRequest;
}

export type WorkerReply = { id: number; ok: true; res: unknown } | { id: number; ok: false; error: string };
