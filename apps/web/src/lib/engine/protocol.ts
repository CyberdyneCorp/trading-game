// Typed request/response protocol between the UI thread and the engine worker.
// Responses carry only current-step state: the worker never sends future bars.

import type {
	AdjustMarginCommand,
	AnswerLessonCommand,
	AnswerQuestionCommand,
	CancelOrderCommand,
	ClientIdCommand,
	CommandResult,
	CsvMeta,
	CsvValidation,
	CsvReport,
	DebriefData,
	EngineVersion,
	GameState,
	LessonInfo,
	MissionInfo,
	MissionReveal,
	MoveOrderCommand,
	PackCheck,
	PlaceOptionOrderCommand,
	PlaceOrderCommand,
	RecordPlanCommand,
	RunOptionsView,
	SetLeverageCommand,
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
	// A mission's lesson questions without the key (tl_lesson, run-less).
	| { kind: 'lesson'; missionId: string }
	// Player commands: payload shapes generated from engine/idl (one C ABI call each).
	| ({ kind: 'order' } & PlaceOrderCommand)
	| ({ kind: 'option' } & PlaceOptionOrderCommand)
	| ({ kind: 'margin' } & AdjustMarginCommand)
	| ({ kind: 'plan' } & RecordPlanCommand)
	| ({ kind: 'answer' } & AnswerQuestionCommand)
	| ({ kind: 'lessonAnswer' } & AnswerLessonCommand)
	| ({ kind: 'cancel' } & CancelOrderCommand)
	| ({ kind: 'move' } & MoveOrderCommand)
	| ({ kind: 'leverage' } & SetLeverageCommand)
	| ({ kind: 'close' } & ClientIdCommand)
	| ({ kind: 'advice' } & ClientIdCommand)
	| { kind: 'advance'; steps: number }
	| { kind: 'debrief' }
	// The current run's seeded reveal (G4); refused while the run is active (design D6).
	| { kind: 'reveal' };

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
	init: { version: EngineVersion; missions: MissionInfo[] };
	deviceKey: TrustedKey;
	csvValidate: CsvValidation;
	csvBuild: CsvBuildResult;
	packValidate: PackCheck;
	start: GameState;
	startPack: GameState;
	advance: GameState;
	debrief: DebriefData;
	/** Null for a mission without lesson questions. */
	lesson: LessonInfo | null;
	/** Null for runs on imported packs (their reveal is in the debrief). */
	reveal: MissionReveal | null;
}

export type EngineResponse<K extends EngineRequest['kind']> = K extends keyof ResponseMap ? ResponseMap[K] : CommandResponse;

export interface WorkerEnvelope {
	id: number;
	req: EngineRequest;
}

export type WorkerReply = { id: number; ok: true; res: unknown } | { id: number; ok: false; error: string };
