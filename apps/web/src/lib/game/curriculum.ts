// Curriculum graph and unlock rules, as data (spec curriculum "Ordered learning stages",
// "Progressive unlocks", "Progress is never destroyed"; add-scenarios-and-gameplay task 5.4).
//
// A stage is passed when each of its assessment missions was played to the end (not bankrupt) in a
// campaign run. Passing is permanent: nothing here ever removes a passed stage. A stage's unlocks
// apply to every later campaign mission; the stage's own assessment mission always has them (it is
// where they are taught and assessed). A stage taught only by Academy lessons (Foundations) is
// passed when its lessons are completed; like a stage with no content yet ("coming soon") it never
// blocks the stages after it, because the campaign does not gate on lessons.

export type StageId =
	| 'foundations'
	| 'structure'
	| 'orders'
	| 'futures1'
	| 'futures2'
	| 'futures3'
	| 'risk'
	| 'options1'
	| 'options2'
	| 'options3'
	| 'patterns'
	| 'events'
	| 'evaluation';

/** Tools a campaign mission may use only once their stage is passed. Never purchasable. */
export type Unlock = 'short' | 'leverage' | 'optionsBuy' | 'optionsWrite';

export interface Stage {
	id: StageId;
	/** Direct prerequisites (spec dependency order). */
	requires: StageId[];
	/** Assessment missions (engine mission ids). */
	missions: string[];
	/** Academy lessons (lessons.ts ids) for stages without a mission; no missions and no lessons = no content yet. */
	lessons: string[];
	unlocks: Unlock[];
}

export const STAGES: readonly Stage[] = [
	{ id: 'foundations', requires: [], missions: [], lessons: ['foundations-bar'], unlocks: [] },
	{ id: 'structure', requires: ['foundations'], missions: ['m06-thin-book'], lessons: [], unlocks: [] },
	{ id: 'orders', requires: ['structure'], missions: ['m06-thin-book'], lessons: [], unlocks: [] },
	{ id: 'futures1', requires: ['orders'], missions: ['m01-false-breakout'], lessons: [], unlocks: ['short'] },
	{ id: 'futures2', requires: ['futures1'], missions: ['m03-volatility-shock'], lessons: [], unlocks: ['leverage'] },
	{ id: 'futures3', requires: ['futures2'], missions: ['m02-funding-squeeze'], lessons: [], unlocks: [] },
	{ id: 'risk', requires: ['futures3'], missions: ['m07-trend-trailing'], lessons: [], unlocks: [] },
	{ id: 'options1', requires: ['risk'], missions: ['m04-options-buyer'], lessons: [], unlocks: ['optionsBuy'] },
	{ id: 'options2', requires: ['options1'], missions: ['m04-options-buyer', 'm09-event-straddle'], lessons: [], unlocks: [] },
	{ id: 'options3', requires: ['options2'], missions: ['m05-options-writer', 'm10-hedged-book'], lessons: [], unlocks: ['optionsWrite'] },
	{ id: 'patterns', requires: ['options3'], missions: ['m08-regime-switch'], lessons: [], unlocks: [] },
	// Events before Evaluation (design D8.4): the capstone builds on the news lesson and comes last.
	{ id: 'events', requires: ['patterns'], missions: ['m11-news-calendar'], lessons: [], unlocks: [] },
	{ id: 'evaluation', requires: ['events'], missions: ['m12-capstone'], lessons: [], unlocks: [] }
];

/** Highest leverage a campaign mission allows before the stage that unlocks `leverage` is passed. */
export const LOCKED_LEVERAGE_CAP = 2;

const byId = new Map(STAGES.map((s) => [s.id, s]));

function stage(id: StageId): Stage {
	const s = byId.get(id);
	if (!s) throw new Error(`unknown stage ${id}`);
	return s;
}

/** Campaign missions in curriculum order (a mission assessing two stages appears once). */
export function campaignMissions(): string[] {
	return [...new Set(STAGES.flatMap((s) => s.missions))];
}

const hasContent = (s: Stage) => s.missions.length > 0 || s.lessons.length > 0;

/**
 * A stage is passed when every assessment mission was finished in a campaign run and every lesson
 * was completed (`lessonsDone`).
 */
export function stagePassed(id: StageId, assessed: readonly string[], lessonsDone: readonly string[] = []): boolean {
	const s = stage(id);
	return hasContent(s) && s.missions.every((m) => assessed.includes(m)) && s.lessons.every((l) => lessonsDone.includes(l));
}

// Cleared = passed, or a stage without a mission (lessons only, or no content yet) whose own
// prerequisites are cleared: lessons never gate the campaign, so they never block a later stage.
function cleared(id: StageId, assessed: readonly string[]): boolean {
	const s = stage(id);
	if (s.missions.length > 0) return stagePassed(id, assessed);
	return s.requires.every((r) => cleared(r, assessed));
}

export type StageStatus = 'passed' | 'open' | 'locked' | 'soon';

export function stageStatus(id: StageId, assessed: readonly string[], lessonsDone: readonly string[] = []): StageStatus {
	const s = stage(id);
	if (!hasContent(s)) return 'soon';
	if (stagePassed(id, assessed, lessonsDone)) return 'passed';
	return s.requires.every((r) => cleared(r, assessed)) ? 'open' : 'locked';
}

/** Stages assessed by a mission. */
export function stagesOf(missionId: string): StageId[] {
	return STAGES.filter((s) => s.missions.includes(missionId)).map((s) => s.id);
}

/** The stage that grants an unlock. */
export function unlockStage(u: Unlock): StageId {
	const s = STAGES.find((x) => x.unlocks.includes(u));
	if (!s) throw new Error(`no stage unlocks ${u}`);
	return s.id;
}

export interface Permissions {
	granted: Unlock[];
	locked: Unlock[];
	/** Engine start option: lowers the mission's leverage cap; null = the mission's own cap. */
	leverageCap: number | null;
}

/** What a campaign mission may use: passed unlocks plus its own stages' unlocks. */
export function campaignPermissions(missionId: string, assessed: readonly string[]): Permissions {
	const own = new Set(stagesOf(missionId));
	const all = STAGES.flatMap((s) => s.unlocks);
	const ok = (u: Unlock) => own.has(unlockStage(u)) || stagePassed(unlockStage(u), assessed);
	const granted = all.filter(ok);
	const locked = all.filter((u) => !ok(u));
	return { granted, locked, leverageCap: locked.includes('leverage') ? LOCKED_LEVERAGE_CAP : null };
}

export interface CurriculumSummary {
	passed: number;
	/** Stages that have content, a mission or a lesson (the others are coming soon). */
	withContent: number;
}

export function curriculumSummary(assessed: readonly string[], lessonsDone: readonly string[] = []): CurriculumSummary {
	const withContent = STAGES.filter(hasContent);
	return { passed: withContent.filter((s) => stagePassed(s.id, assessed, lessonsDone)).length, withContent: withContent.length };
}
