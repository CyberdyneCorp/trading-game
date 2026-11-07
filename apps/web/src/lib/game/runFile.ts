// Run files (openspec add-run-sharing-and-journal-stats, design D1–D3): what the web writes for a run
// and how it names a verdict. The engine checks, replays and seals every file (tl_run_file_verify,
// tl_run_file_seal); this module only gathers the members from the run record and its action log, and
// translates the engine's refusal codes.

import type { PlanEntry } from './analysis.ts';
import type { RunFileReason, RunFileVerdict } from '#lib/engine/types.ts';
import type { RunRecord, StoredPack } from './storage.ts';
import { DEFAULT_RUN_OPTIONS } from '#lib/engine/defaults.ts';

/** The engine's size limit (kRunFileMaxBytes): larger files are refused before they reach it. */
export const RUN_FILE_MAX_BYTES = 1 << 20;

export const RUN_FILE_EXTENSION = '.tlrun';

/** A plan note in a file: amounts as decimal strings (the engine's reader has integers only). */
export interface RunFileNote {
	step: number;
	side: 'buy' | 'sell';
	thesis: string;
	invalidation: string | null;
	target: string | null;
	plannedRisk: string | null;
	confidence: number;
	skipped: boolean;
	loseScenario?: string;
	instrument?: string;
	combo?: string;
}

/** Version 1 (design D1) without `result`, which only the engine writes (tl_run_file_seal). */
export interface RunFileDraft {
	format: 'tradelab-run';
	version: 1;
	mission: string;
	pack: string | null;
	seed: number;
	options: { adviceAllowance: number; newsLead: number; calendar: boolean; startingBalance?: string; leverageCap?: number };
	engine: { executionPolicy: number; abi: number; version: string };
	labels: { campaign: boolean; assisted: boolean };
	actions: unknown[];
	journal: RunFileNote[];
}

export interface RunFile extends RunFileDraft {
	result: { status: 'active' | 'finished' | 'bankrupt'; digest: string; eventsDigest: string };
}

export interface EngineStamp {
	executionPolicy: number;
	abi: number;
	version: string;
}

/** A non-negative amount as the engine's decimal text (at most 8 decimals); null when not a number. */
export function decimalText(v: number | null | undefined): string | null {
	if (v == null || !Number.isFinite(v) || v < 0 || v >= 1e12) return null;
	const s = v.toFixed(8).replace(/0+$/, '');
	return s.endsWith('.') ? s.slice(0, -1) : s;
}

const clip = (s: string, max: number) => {
	const bytes = new TextEncoder().encode(s);
	return bytes.length <= max ? s : new TextDecoder().decode(bytes.slice(0, max)).replace(/�$/, '');
};

export function noteToFile(p: PlanEntry): RunFileNote {
	return {
		step: p.step,
		side: p.side,
		thesis: clip(p.thesis ?? '', 1000),
		invalidation: decimalText(p.invalidation),
		target: decimalText(p.target),
		plannedRisk: decimalText(p.plannedRisk),
		confidence: Math.max(0, Math.min(100, Math.round(p.confidence))),
		skipped: p.skipped,
		...(p.loseScenario ? { loseScenario: clip(p.loseScenario, 1000) } : {}),
		...(p.instrument ? { instrument: clip(p.instrument, 64) } : {}),
		...(p.combo ? { combo: p.combo } : {})
	};
}

export function noteFromFile(n: RunFileNote): PlanEntry {
	const num = (s: string | null) => (s == null ? null : Number(s));
	return {
		step: n.step,
		side: n.side,
		thesis: n.thesis,
		invalidation: num(n.invalidation),
		target: num(n.target),
		plannedRisk: num(n.plannedRisk),
		confidence: n.confidence,
		skipped: n.skipped,
		...(n.loseScenario ? { loseScenario: n.loseScenario } : {}),
		...(n.instrument ? { instrument: n.instrument } : {}),
		...(n.combo ? { combo: n.combo } : {})
	};
}

/**
 * The file's members from a run record and its action log (design D1, D3): no run id, timestamps,
 * device key or progress; plan notes only when asked. The engine adds `result` when it seals it.
 */
export function runFileDraft(record: RunRecord<PlanEntry, unknown>, actions: readonly unknown[], engine: EngineStamp, includeNotes: boolean): RunFileDraft {
	const o = { ...DEFAULT_RUN_OPTIONS, ...record.options };
	return {
		format: 'tradelab-run',
		version: 1,
		mission: record.missionId,
		pack: record.packDigest ?? null,
		seed: record.seed,
		options: {
			adviceAllowance: o.adviceAllowance,
			newsLead: o.newsLead,
			calendar: o.calendar,
			...(o.startingBalance ? { startingBalance: o.startingBalance } : {}),
			...(o.leverageCap ? { leverageCap: o.leverageCap } : {})
		},
		engine: { executionPolicy: record.executionPolicy, abi: engine.abi, version: engine.version },
		labels: { campaign: !!record.campaign, assisted: !!record.assisted },
		actions: [...actions],
		journal: includeNotes ? record.journal.map(noteToFile) : []
	};
}

/** Why a run cannot be exported (null when it can): runs on private packs stay on the device (D3). */
export type ExportBlock = 'private_pack' | 'pack_missing' | 'no_actions';

export function exportBlock(record: Pick<RunRecord, 'packDigest' | 'actions'>, packs: readonly Pick<StoredPack, 'digest' | 'private' | 'origin'>[]): ExportBlock | null {
	if (record.actions === 0) return 'no_actions';
	if (!record.packDigest) return null;
	const pack = packs.find((p) => p.digest === record.packDigest);
	if (!pack) return 'pack_missing';
	return pack.private || pack.origin === 'import' ? 'private_pack' : null;
}

export function runFileName(missionId: string, seed: number): string {
	return `tradelab-${missionId.replace(/[^a-z0-9._-]/gi, '_')}-${seed}${RUN_FILE_EXTENSION}`;
}

/** The parsed canonical file of an accepted verdict. */
export function parseRunFile(text: string): RunFile {
	return JSON.parse(text) as RunFile;
}

/** Verdict reasons the client adds before the engine sees the file. */
export type ClientReason = 'unreadable' | 'unsupported';
export type ImportReason = RunFileReason | ClientReason;

/** The i18n key and parameters that explain a refusal (design D2 codes). */
export function refusalKey(v: Pick<RunFileVerdict, 'path' | 'detail'> & { reason?: ImportReason }, policy: number): { key: string; params: Record<string, string | number> } {
	const reason = v.reason ?? 'malformed';
	if (reason === 'policy_mismatch') return { key: 'runfile.reason.policy_mismatch', params: { file: v.detail ?? '?', engine: policy } };
	if (reason === 'invalid_field') return { key: 'runfile.reason.invalid_field', params: { path: v.path || '?' } };
	return { key: `runfile.reason.${reason}`, params: {} };
}

/** Byte length of a file's UTF-8 text (the engine's limit is in bytes). */
export function utf8Length(text: string): number {
	return new TextEncoder().encode(text).length;
}
