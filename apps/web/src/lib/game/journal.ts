// Journal screen model (web-client task 4.4): the plans the player wrote, run by run. The free text
// stays on this device and is never scored (spec mission-scoring "No subjective LLM grading").

import type { PlanEntry } from './analysis.ts';
import type { RunRecord } from './storage.ts';

export type RunMode = 'campaign' | 'free' | 'practice';

export interface JournalRun {
	runId: string;
	missionId: string;
	seed: number;
	createdAt: number;
	mode: RunMode;
	status: RunRecord['status'];
	entries: PlanEntry[];
}

export function runMode(r: Pick<RunRecord, 'campaign' | 'packDigest'>): RunMode {
	if (r.campaign) return 'campaign';
	return r.packDigest ? 'practice' : 'free';
}

/** Runs with at least one journal entry, newest first, optionally of one mission only. */
export function journalRuns(records: readonly RunRecord<PlanEntry, unknown>[], missionId: string | null = null): JournalRun[] {
	return records
		.filter((r) => r.journal.length > 0 && (missionId == null || r.missionId === missionId))
		.sort((a, b) => b.createdAt - a.createdAt)
		.map((r) => ({
			runId: r.runId,
			missionId: r.missionId,
			seed: r.seed,
			createdAt: r.createdAt,
			mode: runMode(r),
			status: r.status,
			entries: [...r.journal].sort((x, y) => x.step - y.step)
		}));
}

/** Missions that have journal entries, in first-seen order (for the filter). */
export function journalMissions(records: readonly RunRecord<PlanEntry, unknown>[]): string[] {
	return [...new Set(records.filter((r) => r.journal.length > 0).map((r) => r.missionId))];
}

/**
 * Planned risk of the open perpetual position: the last perpetual plan (or skipped plan, which still
 * carries the ticket estimate). The journal is part of the stored run, so the POSIÇÃO panel keeps
 * showing it after a reload and resume (before, it lived only in the terminal view and was lost).
 */
export function positionPlannedRisk(journal: readonly PlanEntry[]): number | null {
	const last = journal.findLast((p) => p.instrument == null);
	return last?.plannedRisk != null && last.plannedRisk > 0 ? last.plannedRisk : null;
}
