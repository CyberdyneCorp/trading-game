// Curriculum graph and unlock rules, as data (spec curriculum "Ordered learning stages",
// "Progressive unlocks", "Progress is never destroyed"; add-scenarios-and-gameplay task 5.4).
//
// A stage is passed when each of its assessment missions was played to the end (not bankrupt) in a
// campaign run. Passing is permanent: nothing here ever removes a passed stage. A stage's unlocks
// apply to every later campaign mission; the stage's own assessment mission always has them (it is
// where they are taught and assessed). Stages without missions yet (their lessons are pending) are
// "coming soon" and do not block the stages after them.

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
	| 'evaluation'
	| 'events';

/** Tools a campaign mission may use only once their stage is passed. Never purchasable. */
export type Unlock = 'short' | 'leverage' | 'optionsBuy' | 'optionsWrite';

export interface Stage {
	id: StageId;
	/** Direct prerequisites (spec dependency order). */
	requires: StageId[];
	/** Assessment missions (engine mission ids); empty = no content yet. */
	missions: string[];
	unlocks: Unlock[];
}

export const STAGES: readonly Stage[] = [
	{ id: 'foundations', requires: [], missions: [], unlocks: [] },
	{ id: 'structure', requires: ['foundations'], missions: [], unlocks: [] },
	{ id: 'orders', requires: ['structure'], missions: [], unlocks: [] },
	{ id: 'futures1', requires: ['orders'], missions: ['m01-false-breakout'], unlocks: ['short'] },
	{ id: 'futures2', requires: ['futures1'], missions: ['m03-volatility-shock'], unlocks: ['leverage'] },
	{ id: 'futures3', requires: ['futures2'], missions: ['m02-funding-squeeze'], unlocks: [] },
	{ id: 'risk', requires: ['futures3'], missions: [], unlocks: [] },
	{ id: 'options1', requires: ['risk'], missions: ['m04-options-buyer'], unlocks: ['optionsBuy'] },
	{ id: 'options2', requires: ['options1'], missions: ['m04-options-buyer'], unlocks: [] },
	{ id: 'options3', requires: ['options2'], missions: ['m05-options-writer'], unlocks: ['optionsWrite'] },
	{ id: 'patterns', requires: ['options3'], missions: [], unlocks: [] },
	{ id: 'evaluation', requires: ['patterns'], missions: [], unlocks: [] },
	{ id: 'events', requires: ['evaluation'], missions: [], unlocks: [] }
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

/** A stage is passed when every assessment mission was finished in a campaign run. */
export function stagePassed(id: StageId, assessed: readonly string[]): boolean {
	const s = stage(id);
	return s.missions.length > 0 && s.missions.every((m) => assessed.includes(m));
}

// Cleared = passed, or a stage with no content yet whose own prerequisites are cleared.
function cleared(id: StageId, assessed: readonly string[]): boolean {
	const s = stage(id);
	if (s.missions.length > 0) return stagePassed(id, assessed);
	return s.requires.every((r) => cleared(r, assessed));
}

export type StageStatus = 'passed' | 'open' | 'locked' | 'soon';

export function stageStatus(id: StageId, assessed: readonly string[]): StageStatus {
	const s = stage(id);
	if (s.missions.length === 0) return 'soon';
	if (stagePassed(id, assessed)) return 'passed';
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
	/** Stages that have content (the others are coming soon). */
	withContent: number;
}

export function curriculumSummary(assessed: readonly string[]): CurriculumSummary {
	const withContent = STAGES.filter((s) => s.missions.length > 0);
	return { passed: withContent.filter((s) => stagePassed(s.id, assessed)).length, withContent: withContent.length };
}
