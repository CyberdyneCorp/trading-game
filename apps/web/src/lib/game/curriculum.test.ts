import { describe, expect, it } from 'vitest';
import {
	campaignMissions,
	campaignPermissions,
	curriculumSummary,
	lessonProgress,
	recommendedLesson,
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
const M06 = 'm06-thin-book';
const M07 = 'm07-trend-trailing';
const M08 = 'm08-regime-switch';
const M09 = 'm09-event-straddle';
const M10 = 'm10-hedged-book';
const M11 = 'm11-news-calendar';
const M12 = 'm12-capstone';
const M13 = 'm13-call-spread';
const M14 = 'm14-covered-call';
const M15 = 'm15-protective-put';
const M16 = 'm16-range-condor';
const M17 = 'm17-before-expiry';
const STRATEGIES = [M13, M14, M15, M16, M17];

// What each built-in mission's engine rules allow (engine/src/missions.cpp): m06 is long only with a
// 2x cap (it comes before Futures I and II); every other mission allows shorting the perpetual and a
// leverage cap above 2x; m04, m09 and m15 list options to buy only (m09 comes before Options III), m05,
// m10, m12 and the strategy missions m13, m14, m16 and m17 also let you write them.
const missionUses: Record<string, Unlock[]> = {
	[M01]: ['short', 'leverage'],
	[M02]: ['short', 'leverage'],
	[M03]: ['short', 'leverage'],
	[M04]: ['short', 'leverage', 'optionsBuy'],
	[M05]: ['short', 'leverage', 'optionsBuy', 'optionsWrite'],
	[M06]: [],
	[M07]: ['short', 'leverage'],
	[M08]: ['short', 'leverage'],
	[M09]: ['short', 'leverage', 'optionsBuy'],
	[M10]: ['short', 'leverage', 'optionsBuy', 'optionsWrite'],
	[M11]: ['short', 'leverage'],
	[M12]: ['short', 'leverage', 'optionsBuy', 'optionsWrite'],
	[M13]: ['short', 'leverage', 'optionsBuy', 'optionsWrite'],
	[M14]: ['short', 'leverage', 'optionsBuy', 'optionsWrite'],
	[M15]: ['short', 'leverage', 'optionsBuy'],
	[M16]: ['short', 'leverage', 'optionsBuy', 'optionsWrite'],
	[M17]: ['short', 'leverage', 'optionsBuy', 'optionsWrite']
};

describe('curriculum graph', () => {
	it('lists the 14 stages in the spec dependency order, Strategies after Options III, each requiring the one before it', () => {
		expect(STAGES.map((s) => s.id)).toEqual([
			'foundations', 'structure', 'orders', 'futures1', 'futures2', 'futures3', 'risk',
			'options1', 'options2', 'options3', 'strategies', 'patterns', 'events', 'evaluation'
		]);
		STAGES.forEach((s, i) => expect(s.requires).toEqual(i === 0 ? [] : [STAGES[i - 1].id]));
	});

	it('orders the campaign by the curriculum: seventeen missions, each once, Strategies after Options III, the capstone last', () => {
		expect(campaignMissions()).toEqual([M06, M01, M03, M02, M07, M04, M09, M05, M10, ...STRATEGIES, M08, M11, M12]);
	});

	it('assesses every stage but Foundations with the D8.4 missions', () => {
		const byStage = Object.fromEntries(STAGES.map((s) => [s.id, s.missions]));
		expect(byStage).toEqual({
			foundations: [],
			structure: [M06],
			orders: [M06],
			futures1: [M01],
			futures2: [M03],
			futures3: [M02],
			risk: [M07],
			options1: [M04],
			options2: [M04, M09],
			options3: [M05, M10],
			strategies: STRATEGIES,
			patterns: [M08],
			events: [M11],
			evaluation: [M12]
		});
	});

	it('each unlock belongs to exactly one stage', () => {
		for (const u of ['short', 'leverage', 'optionsBuy', 'optionsWrite'] as Unlock[]) {
			expect(STAGES.filter((s) => s.unlocks.includes(u))).toHaveLength(1);
		}
	});
});

describe('gating', () => {
	it('Stage taught by a lesson: on a fresh profile Foundations is open (its lesson) and Structure is open too', () => {
		expect(stageStatus('foundations', [])).toBe('open');
		expect(stageStatus('structure', [])).toBe('open');
		expect(stageStatus('orders', [])).toBe('locked');
		expect(stageStatus('futures1', [])).toBe('locked');
		expect(stageStatus('evaluation', [])).toBe('locked');
		expect(stageStatus('events', [])).toBe('locked');
	});

	it('Lesson not done: m03 passes Futures II and unlocks leverage without its lesson', () => {
		expect(STAGES.find((s) => s.id === 'futures2')!.lessons).toEqual(['futures-margin']);
		expect(stagePassed('futures2', [M03], [])).toBe(true);
		expect(campaignPermissions(M02, [M01, M03]).leverageCap).toBeNull();
		// No lesson, done or not, ever passes a stage with missions or locks one.
		for (const s of STAGES.filter((x) => x.missions.length)) {
			expect(stagePassed(s.id, [], s.lessons)).toBe(false);
			expect(stagePassed(s.id, s.missions, [])).toBe(true);
		}
	});

	it('lessons x/y and the recommended lesson of a stage', () => {
		expect(lessonProgress('risk', ['risk-expectancy'])).toEqual({ done: 1, total: 2 });
		expect(recommendedLesson('risk', [])).toBe('risk-sizing');
		expect(recommendedLesson('risk', ['risk-sizing'])).toBe('risk-expectancy');
		expect(recommendedLesson('risk', ['risk-sizing', 'risk-expectancy'])).toBe('risk-sizing');
	});

	it('Foundations passes with its lesson; the lesson never gates the stages after it', () => {
		expect(STAGES[0].lessons).toEqual(['foundations-bar']);
		expect(stageStatus('foundations', [], ['foundations-bar'])).toBe('passed');
		expect(stagePassed('foundations', [M06, M01])).toBe(false);
		expect(stageStatus('structure', [], [])).toBe('open');
		expect(stageStatus('futures1', [M06], [])).toBe('open');
	});

	it('passing a stage opens the next one; m06 passes Structure and Orders at once', () => {
		expect(stageStatus('structure', [M06])).toBe('passed');
		expect(stageStatus('orders', [M06])).toBe('passed');
		expect(stageStatus('futures1', [M06])).toBe('open');
		expect(stageStatus('futures2', [M06, M01])).toBe('open');
		expect(stageStatus('options1', [M06, M01, M03, M02])).toBe('locked'); // Risk sizing not passed yet
		expect(stageStatus('options1', [M06, M01, M03, M02, M07])).toBe('open');
		expect(stageStatus('patterns', [M06, M01, M03, M02, M07, M04, M09, M05])).toBe('locked'); // Options III needs m10 too
		expect(stageStatus('strategies', [M06, M01, M03, M02, M07, M04, M09, M05, M10])).toBe('open');
		expect(stageStatus('patterns', [M06, M01, M03, M02, M07, M04, M09, M05, M10])).toBe('locked'); // Strategies first
		expect(stageStatus('patterns', [M06, M01, M03, M02, M07, M04, M09, M05, M10, ...STRATEGIES])).toBe('open');
	});

	it('Strategies opens after Options III; m13 is then the next campaign mission; it unlocks nothing new', () => {
		const optionsIII = [M06, M01, M03, M02, M07, M04, M09, M05, M10];
		expect(stageStatus('strategies', optionsIII.slice(0, -1))).toBe('locked');
		expect(stageStatus('strategies', optionsIII)).toBe('open');
		const order = campaignMissions();
		expect(order[order.indexOf(M10) + 1]).toBe(M13);
		expect(STAGES.find((s) => s.id === 'strategies')?.unlocks).toEqual([]);
		expect(stagePassed('strategies', [...optionsIII, M13, M14, M15, M16])).toBe(false); // m17 still to come
		expect(stagePassed('strategies', [...optionsIII, ...STRATEGIES])).toBe(true);
		for (const m of STRATEGIES) expect(campaignPermissions(m, optionsIII).locked, m).toEqual([]);
	});

	it('A stage with two missions: m04 passes Options I; Options II stays open until m09, Options III locked', () => {
		const assessed = [M06, M01, M03, M02, M07, M04];
		expect(stagePassed('options1', assessed)).toBe(true);
		expect(stageStatus('options2', assessed)).toBe('open');
		expect(stageStatus('options3', assessed)).toBe('locked');
		expect(stageStatus('options2', [...assessed, M09])).toBe('passed');
		expect(stageStatus('options3', [...assessed, M09])).toBe('open');
	});

	it('Capstone last: Events comes before Evaluation, and m12 is the last campaign mission', () => {
		const beforeEvents = [M06, M01, M03, M02, M07, M04, M09, M05, M10, ...STRATEGIES, M08];
		expect(stageStatus('events', beforeEvents)).toBe('open');
		expect(stageStatus('evaluation', beforeEvents)).toBe('locked');
		expect(stageStatus('evaluation', [...beforeEvents, M11])).toBe('open');
		expect(campaignMissions().at(-1)).toBe(M12);
	});

	it('Writing locked: before Options III is passed, options writing is locked in other campaign missions', () => {
		const assessed = [M01, M03, M02, M04];
		expect(campaignPermissions(M02, assessed).locked).toEqual(['optionsWrite']);
		expect(campaignPermissions(M04, assessed).locked).toEqual(['optionsWrite']);
		expect(campaignPermissions(M02, [...assessed, M09, M05, M10]).locked).toEqual([]);
		expect(campaignPermissions(M02, [...assessed, M05]).locked).toEqual(['optionsWrite']); // m10 still to come
	});

	it('No lock the engine cannot enforce: m09 lists option writing as locked and its own rules forbid it', () => {
		const { locked, granted } = campaignPermissions(M09, [M06, M01, M03, M02, M07, M04]);
		expect(locked).toEqual(['optionsWrite']);
		expect(granted).toContain('optionsBuy');
		expect(missionUses[M09]).not.toContain('optionsWrite');
		expect(campaignPermissions(M10, [M06, M01, M03, M02, M07, M04, M09, M05]).granted).toContain('optionsWrite');
	});

	it("a stage's own assessment mission has its unlocks (it is where they are taught)", () => {
		expect(campaignPermissions(M05, [M01, M03, M02, M04]).granted).toContain('optionsWrite');
		expect(campaignPermissions(M03, [M01]).leverageCap).toBeNull(); // Futures II: leverage is the lesson
	});

	it('m06, the first campaign mission, allows no shorting and keeps the 2x cap: its locks need nothing new', () => {
		const { locked, leverageCap } = campaignPermissions(M06, []);
		expect(locked).toEqual(['short', 'leverage', 'optionsBuy', 'optionsWrite']);
		expect(leverageCap).toBe(LOCKED_LEVERAGE_CAP); // the same as m06's own cap
		expect(missionUses[M06]).toEqual([]);
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
		expect(curriculumSummary([])).toEqual({ passed: 0, withContent: 14 });
		expect(curriculumSummary([M01, M03])).toEqual({ passed: 2, withContent: 14 });
		expect(curriculumSummary([M04])).toEqual({ passed: 1, withContent: 14 }); // Options I; Options II needs m09
		expect(curriculumSummary([M04, M09])).toEqual({ passed: 2, withContent: 14 }); // Options I and II
		expect(curriculumSummary([M06])).toEqual({ passed: 2, withContent: 14 }); // Structure and Orders
		expect(curriculumSummary([M11, M12])).toEqual({ passed: 2, withContent: 14 }); // Events and Evaluation
		expect(curriculumSummary([M06], ['foundations-bar'])).toEqual({ passed: 3, withContent: 14 }); // + Foundations by its lesson
	});
});
