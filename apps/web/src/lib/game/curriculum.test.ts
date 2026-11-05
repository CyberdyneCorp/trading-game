import { describe, expect, it } from 'vitest';
import {
	campaignMissions,
	campaignPermissions,
	curriculumSummary,
	LOCKED_LEVERAGE_CAP,
	STAGES,
	stagePassed,
	stageStatus,
	type Unlock
} from './curriculum.ts';

const M01 = 'm01-false-breakout';
const M02 = 'm02-funding-squeeze';
const M03 = 'm03-volatility-shock';
const M04 = 'm04-options-buyer';
const M05 = 'm05-options-writer';

// What each built-in mission's engine rules allow (engine/src/missions.cpp): every mission allows
// shorting the perpetual and a leverage cap above 2x; m04 lists options, m05 also lets you write them.
const missionUses: Record<string, Unlock[]> = {
	[M01]: ['short', 'leverage'],
	[M02]: ['short', 'leverage'],
	[M03]: ['short', 'leverage'],
	[M04]: ['short', 'leverage', 'optionsBuy'],
	[M05]: ['short', 'leverage', 'optionsBuy', 'optionsWrite']
};

describe('curriculum graph', () => {
	it('lists the 13 stages in the spec dependency order, each requiring the one before it', () => {
		expect(STAGES.map((s) => s.id)).toEqual([
			'foundations', 'structure', 'orders', 'futures1', 'futures2', 'futures3', 'risk',
			'options1', 'options2', 'options3', 'patterns', 'evaluation', 'events'
		]);
		STAGES.forEach((s, i) => expect(s.requires).toEqual(i === 0 ? [] : [STAGES[i - 1].id]));
	});

	it('orders the campaign by the curriculum (Futures II before Futures III), each mission once', () => {
		expect(campaignMissions()).toEqual([M01, M03, M02, M04, M05]);
	});

	it('each unlock belongs to exactly one stage', () => {
		for (const u of ['short', 'leverage', 'optionsBuy', 'optionsWrite'] as Unlock[]) {
			expect(STAGES.filter((s) => s.unlocks.includes(u))).toHaveLength(1);
		}
	});
});

describe('gating', () => {
	it('on a fresh profile only Futures I is open; later stages are locked, empty ones coming soon', () => {
		expect(stageStatus('futures1', [])).toBe('open');
		expect(stageStatus('futures2', [])).toBe('locked');
		expect(stageStatus('options3', [])).toBe('locked');
		expect(stageStatus('foundations', [])).toBe('soon');
		expect(stageStatus('risk', [])).toBe('soon');
	});

	it('passing a stage opens the next one; stages without content do not block (Risk sizing)', () => {
		expect(stageStatus('futures1', [M01])).toBe('passed');
		expect(stageStatus('futures2', [M01])).toBe('open');
		expect(stageStatus('options1', [M01, M03])).toBe('locked'); // Futures III not passed yet
		expect(stageStatus('options1', [M01, M03, M02])).toBe('open');
	});

	it('a mission assessing two stages passes both (m04: Options I and II)', () => {
		expect(stagePassed('options1', [M04])).toBe(true);
		expect(stagePassed('options2', [M04])).toBe(true);
		expect(stagePassed('risk', [M01, M02, M03, M04, M05])).toBe(false); // no content: never "passed"
	});

	it('Writing locked: before Options III is passed, options writing is locked in other campaign missions', () => {
		const assessed = [M01, M03, M02, M04];
		expect(campaignPermissions(M02, assessed).locked).toEqual(['optionsWrite']);
		expect(campaignPermissions(M04, assessed).locked).toEqual(['optionsWrite']);
		expect(campaignPermissions(M02, [...assessed, M05]).locked).toEqual([]);
	});

	it("a stage's own assessment mission has its unlocks (it is where they are taught)", () => {
		expect(campaignPermissions(M05, [M01, M03, M02, M04]).granted).toContain('optionsWrite');
		expect(campaignPermissions(M03, [M01]).leverageCap).toBeNull(); // Futures II: leverage is the lesson
	});

	it('leverage above 2x stays locked in campaign missions until Futures II is passed', () => {
		expect(campaignPermissions(M01, []).leverageCap).toBe(LOCKED_LEVERAGE_CAP);
		expect(campaignPermissions(M01, [M01]).leverageCap).toBe(LOCKED_LEVERAGE_CAP);
		expect(campaignPermissions(M01, [M01, M03]).leverageCap).toBeNull();
	});

	it('every lock a campaign mission meets on its way is one the engine enforces (only the leverage cap)', () => {
		// Played in order, mission k has missions 0..k-1 assessed; a new campaign run keeps what was
		// assessed before, so the first run (nothing assessed) is the strictest case.
		const order = campaignMissions();
		order.forEach((m, k) => {
			const { locked } = campaignPermissions(m, order.slice(0, k));
			const needsEngine = locked.filter((u) => missionUses[m].includes(u) && u !== 'leverage');
			expect(needsEngine, m).toEqual([]);
		});
	});
});

describe('summary', () => {
	it('counts passed stages among the stages that have content', () => {
		expect(curriculumSummary([])).toEqual({ passed: 0, withContent: 6 });
		expect(curriculumSummary([M01, M03])).toEqual({ passed: 2, withContent: 6 });
		expect(curriculumSummary([M04])).toEqual({ passed: 2, withContent: 6 }); // Options I and II
	});
});
