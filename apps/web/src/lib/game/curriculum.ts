// Curriculum graph and unlock rules, as data (spec curriculum "Ordered learning stages",
// "Progressive unlocks", "Progress is never destroyed"; add-scenarios-and-gameplay task 5.4).
//
// A stage with assessment missions is passed when each of them was played to the end (not bankrupt)
// in a campaign run; its Academy lessons prepare for it and never gate it (add-academy-and-strategies
// design D9). Passing is permanent: nothing here ever removes a passed stage. A stage's unlocks
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
	| 'strategies'
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
	/** Academy lessons (content/academy ids), in order; no missions and no lessons = no content yet. */
	lessons: string[];
	unlocks: Unlock[];
}

export const STAGES: readonly Stage[] = [
	{ id: 'foundations', requires: [], missions: [], lessons: ['foundations-bar'], unlocks: [] },
	{ id: 'structure', requires: ['foundations'], missions: ['m06-thin-book'], lessons: ['structure-zones'], unlocks: [] },
	{ id: 'orders', requires: ['structure'], missions: ['m06-thin-book'], lessons: ['orders-execution'], unlocks: [] },
	{ id: 'futures1', requires: ['orders'], missions: ['m01-false-breakout'], lessons: ['futures-pnl'], unlocks: ['short'] },
	{ id: 'futures2', requires: ['futures1'], missions: ['m03-volatility-shock'], lessons: ['futures-margin'], unlocks: ['leverage'] },
	{ id: 'futures3', requires: ['futures2'], missions: ['m02-funding-squeeze'], lessons: ['futures-funding'], unlocks: [] },
	{ id: 'risk', requires: ['futures3'], missions: ['m07-trend-trailing'], lessons: ['risk-sizing', 'risk-expectancy'], unlocks: [] },
	{ id: 'options1', requires: ['risk'], missions: ['m04-options-buyer'], lessons: ['options-payoff'], unlocks: ['optionsBuy'] },
	{ id: 'options2', requires: ['options1'], missions: ['m04-options-buyer', 'm09-event-straddle'], lessons: ['options-greeks', 'options-iv'], unlocks: [] },
	{ id: 'options3', requires: ['options2'], missions: ['m05-options-writer', 'm10-hedged-book'], lessons: ['options-writing'], unlocks: ['optionsWrite'] },
	// Strategies (add-academy-and-strategies design D8): option structures after Options III, which
	// already unlocks writing; m15 buys only. The capstone stays last.
	{
		id: 'strategies',
		requires: ['options3'],
		missions: ['m13-call-spread', 'm14-covered-call', 'm15-protective-put', 'm16-range-condor', 'm17-before-expiry'],
		lessons: ['strategies-shapes'],
		unlocks: []
	},
	{ id: 'patterns', requires: ['strategies'], missions: ['m08-regime-switch'], lessons: ['patterns-lag'], unlocks: [] },
	// Events before Evaluation (design D8.4): the capstone builds on the news lesson and comes last.
	{ id: 'events', requires: ['patterns'], missions: ['m11-news-calendar'], lessons: ['events-surprise'], unlocks: [] },
	{ id: 'evaluation', requires: ['events'], missions: ['m12-capstone'], lessons: ['mind-plan'], unlocks: [] }
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
 * A stage with missions is passed when every assessment mission was finished in a campaign run (its
 * lessons never gate it); a lesson-only stage when every lesson was completed (`lessonsDone`).
 */
export function stagePassed(id: StageId, assessed: readonly string[], lessonsDone: readonly string[] = []): boolean {
	const s = stage(id);
	if (s.missions.length > 0) return s.missions.every((m) => assessed.includes(m));
	return s.lessons.length > 0 && s.lessons.every((l) => lessonsDone.includes(l));
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

/**
 * The stages a finished mission leaves passed: its stages whose missions are now all assessed. A stage
 * with several missions (Options II, Options III, Strategies) is passed by its last one, not its first.
 */
export function stagesPassedWith(missionId: string, assessed: readonly string[]): StageId[] {
	return stagesOf(missionId).filter((s) => stagePassed(s, assessed));
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

/** A stage's Academy lessons done, out of its total (curriculum map "lessons x/y"). */
export function lessonProgress(id: StageId, lessonsDone: readonly string[]): { done: number; total: number } {
	const s = stage(id);
	return { done: s.lessons.filter((l) => lessonsDone.includes(l)).length, total: s.lessons.length };
}

/** The lesson a recommendation opens: the stage's first lesson not completed, else its first (null: none). */
export function recommendedLesson(id: StageId, lessonsDone: readonly string[]): string | null {
	const s = stage(id);
	return s.lessons.find((l) => !lessonsDone.includes(l)) ?? s.lessons[0] ?? null;
}

/** The stage a debrief's next-lesson code recommends (engine LessonCode; "options" is Options II). */
export function lessonCodeStage(code: string): StageId | null {
	if (code === 'options') return 'options2';
	return byId.has(code as StageId) ? (code as StageId) : null;
}
