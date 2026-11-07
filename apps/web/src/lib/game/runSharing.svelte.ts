// Run files and journal statistics, the session's side (add-run-sharing-and-journal-stats D2, D5, D7):
// export (draft from the run record and its log, sealed by the engine), import (verified by the
// engine, kept in the separate `imports` store) and recomputing the summaries of older runs. Watching a
// run lives in the session (it drives the session's engine run, design D6).

import type { RunFileReply } from '#lib/engine/protocol.ts';
import type { PlanEntry } from './analysis.ts';
import { missingSummaries } from './journalStats.ts';
import { summarize } from './runSummary.ts';
import { exportBlock, parseRunFile, RUN_FILE_MAX_BYTES, runFileDraft, runFileName, utf8Length, type EngineStamp, type ExportBlock, type ImportReason } from './runFile.ts';
import type { ImportRecord, RunRecord, RunStore, StoredPack } from './storage.ts';

/** What the session provides: run-less engine calls (it supplies the pack and the trusted keys). */
export interface SharingHost {
	runFileCall(kind: 'verifyRun' | 'sealRun', text: string, pack: string | null): Promise<RunFileReply>;
	readonly sharingStore: RunStore<unknown, PlanEntry, never> | null;
	readonly packs: readonly Pick<StoredPack, 'digest' | 'private' | 'origin'>[];
	engineStamp(): EngineStamp;
}

export type ImportOutcome =
	| { ok: true; record: ImportRecord }
	| { ok: false; reason: ImportReason; path?: string; detail?: string; pack?: string };

export type ExportOutcome =
	| { ok: true; name: string; text: string; status: string; mismatch: boolean }
	| { ok: false; reason: ExportBlock | ImportReason; detail?: string };

/** Refused verdict → outcome (the engine's reason, path, detail and the pack it needs). */
function refused(v: RunFileReply): { ok: false; reason: ImportReason; path?: string; detail?: string; pack?: string } {
	return { ok: false, reason: v.reason ?? 'malformed', path: v.path || undefined, detail: v.detail || undefined, pack: v.pack || undefined };
}

export class RunSharing {
	/** Imported runs, newest first (loaded with the journal). */
	imports = $state<ImportRecord[]>([]);

	constructor(private host: SharingHost) {}

	async loadImports(): Promise<void> {
		this.imports = (await this.host.sharingStore?.imports().catch(() => null)) ?? [];
	}

	/**
	 * Verifies a file and stores it (design D2, D5). A file that names a catalog pack is verified
	 * again with that pack when it is installed; otherwise the outcome names the pack to install.
	 */
	async importText(text: string): Promise<ImportOutcome> {
		if (utf8Length(text) > RUN_FILE_MAX_BYTES) return { ok: false, reason: 'too_large' };
		let v = await this.host.runFileCall('verifyRun', text, null);
		if (!v.ok && v.reason === 'pack_required' && this.host.packs.some((p) => p.digest === v.pack)) {
			v = await this.host.runFileCall('verifyRun', text, v.pack);
		}
		if (!v.ok || !v.run || !v.fileDigest) return refused(v);
		const record: ImportRecord = {
			id: v.fileDigest,
			text: v.run,
			mission: v.mission ?? '',
			seed: v.seed ?? 0,
			pack: v.pack || null,
			status: v.status ?? 'active',
			importedAt: Date.now()
		};
		await this.host.sharingStore?.saveImport(record);
		await this.loadImports();
		return { ok: true, record };
	}

	async removeImport(id: string): Promise<void> {
		await this.host.sharingStore?.deleteImport(id).catch(() => undefined);
		await this.loadImports();
	}

	/** The canonical file of one of the player's runs, sealed by the engine (null `text` on refusal). */
	async sealOwn(runId: string, includeNotes: boolean): Promise<{ v: RunFileReply; record: RunRecord<PlanEntry, never> | null; block?: ExportBlock }> {
		const stored = await this.host.sharingStore?.storedRun(runId).catch(() => null);
		if (!stored) return { v: { ok: false, reason: 'malformed', pack: '' }, record: null };
		const record = stored.record as RunRecord<PlanEntry, never>;
		const block = exportBlock({ packDigest: record.packDigest, actions: stored.actions.length }, this.host.packs);
		if (block) return { v: { ok: false, pack: '' }, record, block };
		const draft = runFileDraft(record, stored.actions, this.host.engineStamp(), includeNotes);
		return { v: await this.host.runFileCall('sealRun', JSON.stringify(draft), record.packDigest ?? null), record };
	}

	/**
	 * Exports one of the player's runs (design D1–D3). `liveDigest` is the state digest the client shows
	 * for it (the current run), else the one stored on its record; a sealed digest that differs means
	 * the local log no longer reproduces it, and the outcome says so.
	 */
	async exportRun(runId: string, includeNotes: boolean, liveDigest?: string): Promise<ExportOutcome> {
		const { v, record, block } = await this.sealOwn(runId, includeNotes);
		if (block) return { ok: false, reason: block };
		if (!v.ok || !v.run || !record) return { ok: false, reason: v.reason ?? 'malformed', detail: v.detail || undefined };
		const known = liveDigest ?? record.digest;
		return { ok: true, name: runFileName(record.missionId, record.seed), text: v.run + '\n', status: v.status ?? 'active', mismatch: !!known && known !== v.digest };
	}

	/** An imported run's file, exported again exactly as stored. */
	exportImport(rec: ImportRecord): { name: string; text: string } {
		return { name: runFileName(rec.mission, rec.seed), text: rec.text + '\n' };
	}

	/**
	 * Summaries for ended runs that have none (finished before statistics existed): each is sealed by
	 * the engine, whose verdict carries the debrief. A run of another policy, on a missing or private
	 * pack, or whose replay no longer gives its stored digest is skipped.
	 */
	async recomputeSummaries(records: readonly RunRecord<PlanEntry, never>[], onProgress: (done: number, total: number) => void = () => {}): Promise<{ done: number; skipped: number }> {
		const todo = missingSummaries(records);
		let done = 0;
		let skipped = 0;
		for (const r of todo) {
			const ok = await this.recomputeOne(r);
			done += ok ? 1 : 0;
			skipped += ok ? 0 : 1;
			onProgress(done + skipped, todo.length);
		}
		return { done, skipped };
	}

	private async recomputeOne(r: RunRecord<PlanEntry, never>): Promise<boolean> {
		const { v } = await this.sealOwn(r.runId, false).catch(() => ({ v: null }));
		const summary = v?.ok && v.debrief && (!r.digest || r.digest === v.digest) ? summarize(v.debrief) : null;
		if (!summary) return false;
		await this.host.sharingStore?.updateRun(r.runId, { summary }).catch(() => undefined);
		return true;
	}
}

/** The parsed canonical text of an import (for its labels in a list). */
export function importLabels(rec: ImportRecord): { campaign: boolean; assisted: boolean; notes: number } {
	try {
		const f = parseRunFile(rec.text);
		return { campaign: f.labels.campaign, assisted: f.labels.assisted, notes: f.journal.length };
	} catch {
		return { campaign: false, assisted: false, notes: 0 };
	}
}
