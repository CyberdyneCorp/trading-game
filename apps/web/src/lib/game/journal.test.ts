import { describe, expect, it } from 'vitest';
import type { PlanEntry } from './analysis.ts';
import { journalMissions, journalRuns, positionPlannedRisk, runMode } from './journal.ts';
import type { RunRecord } from './storage.ts';

const plan = (step: number, thesis: string): PlanEntry => ({
	step, side: 'buy', thesis, invalidation: 95, target: 110, confidence: 60, plannedRisk: 100, skipped: false
});

function rec(runId: string, missionId: string, createdAt: number, journal: PlanEntry[], extra: Partial<RunRecord<PlanEntry, unknown>> = {}): RunRecord<PlanEntry, unknown> {
	return {
		runId, missionId, seed: 1, createdAt, executionPolicy: 12, journal, questions: [], stopWarnings: 0, digest: '',
		status: 'finished', actions: 0, ...extra
	};
}

describe('journal', () => {
	const records = [
		rec('a', 'm01-false-breakout', 1, [plan(70, 'late'), plan(61, 'early')]),
		rec('b', 'm02-funding-squeeze', 3, [plan(55, 'trend')], { campaign: { id: 'c', index: 2 } }),
		rec('c', 'm01-false-breakout', 2, []),
		rec('d', 'csv-x', 4, [plan(40, 'known')], { packDigest: 'x', status: 'active' })
	];

	it('lists runs with entries newest first, entries in bar order', () => {
		const runs = journalRuns(records);
		expect(runs.map((r) => r.runId)).toEqual(['d', 'b', 'a']);
		expect(runs[2].entries.map((e) => e.thesis)).toEqual(['early', 'late']);
	});

	it('labels the mode of each run', () => {
		expect(journalRuns(records).map((r) => r.mode)).toEqual(['practice', 'campaign', 'free']);
		expect(runMode({})).toBe('free');
	});

	it('labels runs played with an intel perk (start options) "with intel"', () => {
		const intel = [
			rec('w', 'm01-false-breakout', 5, [plan(70, 'x')], { options: { adviceAllowance: 3, newsLead: 1, calendar: false } }),
			rec('k', 'm02-funding-squeeze', 6, [plan(70, 'y')], { options: { adviceAllowance: 3, newsLead: 0, calendar: true } }),
			rec('p', 'm01-false-breakout', 7, [plan(70, 'z')], { options: { adviceAllowance: 3, newsLead: 0, calendar: false } })
		];
		expect(journalRuns([...records, ...intel]).map((r) => [r.runId, r.intel])).toEqual([
			['p', false], ['k', true], ['w', true], ['d', false], ['b', false], ['a', false]
		]);
	});

	it('filters by mission and offers only missions that have entries', () => {
		expect(journalRuns(records, 'm01-false-breakout').map((r) => r.runId)).toEqual(['a']);
		expect(journalMissions(records)).toEqual(['m01-false-breakout', 'm02-funding-squeeze', 'csv-x']);
	});

	it('regression: the position planned risk comes from the stored journal, so it survives a resume', () => {
		const option: PlanEntry = { ...plan(80, 'call'), plannedRisk: 30, instrument: 'C 100 · exp 96' };
		const skipped: PlanEntry = { ...plan(75, ''), plannedRisk: 42.5, skipped: true };
		expect(positionPlannedRisk([plan(60, 'a'), skipped, option])).toBe(42.5);
		expect(positionPlannedRisk([option])).toBeNull();
		expect(positionPlannedRisk([])).toBeNull();
	});
});
