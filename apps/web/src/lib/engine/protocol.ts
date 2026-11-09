// Typed request/response protocol between the UI thread and the engine worker.
// Responses carry only current-step state: the worker never sends future bars.

import type {
	AdjustMarginCommand,
	AnswerLessonCommand,
	AnswerQuestionCommand,
	CancelOrderCommand,
	ClientIdCommand,
	ComboPreview,
	ComboRequest,
	CommandResult,
	CsvMeta,
	CsvValidation,
	CsvReport,
	DebriefData,
	EngineVersion,
	GameState,
	LessonInfo,
	LinkOcoCommand,
	MissionInfo,
	MissionReveal,
	MoveOrderCommand,
	PackCheck,
	PlaceOptionOrderCommand,
	PlaceOrderCommand,
	RecordPlanCommand,
	RunFileVerdict,
	RunOptionsView,
	SetLeverageCommand,
	TrustedKey,
	WhatIfRequest,
	WhatIfResult
} from './types';

export type EngineRequest =
	// Loads the engine build at `engineUrl` (this version's, or a run's pinned older build).
	| { kind: 'init'; engineUrl: string }
	| { kind: 'useEngine'; engineUrl: string }
	| { kind: 'start'; missionId: string; seed: number; options?: RunOptionsView }
	// Packs and CSV import: bytes cross the worker boundary, prices only as tl_state reveals them.
	| { kind: 'deviceKey'; seedHex: string }
	| { kind: 'csvValidate'; csv: string; meta: CsvMeta }
	| { kind: 'csvBuild'; csv: string; meta: CsvMeta; seedHex: string }
	| { kind: 'packValidate'; pack: Uint8Array; keys: TrustedKey[] }
	| { kind: 'startPack'; pack: Uint8Array; keys: TrustedKey[]; seed: number }
	// A mission's lesson questions without the key (tl_lesson, run-less).
	| { kind: 'lesson'; missionId: string }
	// Report-only option analysis (add-academy-and-strategies D4): never logged, never changes the run.
	| { kind: 'whatif'; request: WhatIfRequest }
	| { kind: 'runWhatif'; request: WhatIfRequest }
	| { kind: 'comboPreview'; request: ComboRequest }
	// Run files (add-run-sharing-and-journal-stats D2): run-less, never touch the current run.
	| { kind: 'verifyRun'; text: string; pack?: Uint8Array; keys?: TrustedKey[] }
	| { kind: 'sealRun'; text: string; pack?: Uint8Array; keys?: TrustedKey[] }
	// Player commands: payload shapes generated from engine/idl (one C ABI call each).
	| ({ kind: 'order' } & PlaceOrderCommand)
	| ({ kind: 'option' } & PlaceOptionOrderCommand)
	| ({ kind: 'margin' } & AdjustMarginCommand)
	| ({ kind: 'plan' } & RecordPlanCommand)
	| ({ kind: 'answer' } & AnswerQuestionCommand)
	| ({ kind: 'lessonAnswer' } & AnswerLessonCommand)
	| ({ kind: 'cancel' } & CancelOrderCommand)
	| ({ kind: 'move' } & MoveOrderCommand)
	// Policy v18 (add-practice-and-advanced-strategies D7): refused as 'unsupported' by an older build.
	| ({ kind: 'oco' } & LinkOcoCommand)
	| ({ kind: 'leverage' } & SetLeverageCommand)
	| ({ kind: 'close' } & ClientIdCommand)
	| ({ kind: 'advice' } & ClientIdCommand)
	| { kind: 'advance'; steps: number }
	| { kind: 'debrief' }
	// The current run's seeded reveal (G4); refused while the run is active (design D6).
	| { kind: 'reveal' }
	// Checkpoints (opaque engine RunState: no scenario bars) and restore + replay of the tail.
	| { kind: 'checkpoint' }
	| { kind: 'restore'; missionId: string; seed: number; checkpoint: string; pack?: Uint8Array; keys?: TrustedKey[] };

/**
 * An analysis reply: the engine's result, or why there is none ('unsupported' when the loaded engine
 * build predates the call, e.g. a run pinned to an older build; otherwise the engine's error code).
 */
export type Analysis<T> = { ok: true; value: T } | { ok: false; error: string };

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
	useEngine: EngineVersion;
	checkpoint: { checkpoint: string; step: number };
	/** Null when the engine refuses the checkpoint (another format, scenario or a damaged one). */
	restore: GameState | null;
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
	whatif: Analysis<WhatIfResult>;
	runWhatif: Analysis<WhatIfResult>;
	comboPreview: Analysis<ComboPreview>;
	/** Null for runs on imported packs (their reveal is in the debrief). */
	reveal: MissionReveal | null;
	verifyRun: RunFileReply;
	sealRun: RunFileReply;
}

/** A run-file verdict, or `unsupported` from a build that predates run files. */
export type RunFileReply = Omit<RunFileVerdict, 'reason'> & { reason?: RunFileVerdict['reason'] | 'unsupported' };

export type EngineResponse<K extends EngineRequest['kind']> = K extends keyof ResponseMap ? ResponseMap[K] : CommandResponse;

export interface WorkerEnvelope {
	id: number;
	req: EngineRequest;
}

export type WorkerReply = { id: number; ok: true; res: unknown } | { id: number; ok: false; error: string };
