// Campaign runs (spec gameplay-loop "Game over on bankruptcy"; add-scenarios-and-gameplay task 4.7).
//
// A campaign run plays the curriculum's missions in order. Each mission starts from the capital the
// previous one ended with (the engine's final equity, passed back as the `startingBalance` start
// option), so the engine stays the only source of balances. Bankruptcy ends the campaign run; a new
// one starts again from CAMPAIGN_CAPITAL. Finishing a mission in a campaign passes its curriculum
// stages for good (`assessed`): ending a campaign run never removes them.
// Leaving a campaign mission for another run carries the equity it had when it was left, and the
// campaign stays on that mission, so leaving never restores lost capital.
// A mission that leaves less than CAMPAIGN_MIN_CAPITAL counts as a bankruptcy: the engine only ends a
// run when the minimum order is unaffordable at that mission's own leverage cap, and the next mission
// may cap leverage lower (a liquidation at 20x can leave crumbs that no 2x-10x mission can trade).

import type { LegReport } from './campaignReport.ts';
import { campaignMissions } from './curriculum.ts';

/**
 * The 17-mission campaign of add-academy-and-strategies, the order of every campaign run that started
 * before progress version 4 (design D14 "Campaign order snapshot"): such a run keeps it, so its stored
 * index never jumps to another mission.
 */
export const CAMPAIGN_ORDER_V3: readonly string[] = [
	'm06-thin-book',
	'm01-false-breakout',
	'm03-volatility-shock',
	'm02-funding-squeeze',
	'm07-trend-trailing',
	'm04-options-buyer',
	'm09-event-straddle',
	'm05-options-writer',
	'm10-hedged-book',
	'm13-call-spread',
	'm14-covered-call',
	'm15-protective-put',
	'm16-range-condor',
	'm17-before-expiry',
	'm08-regime-switch',
	'm11-news-calendar',
	'm12-capstone'
];

export const CAMPAIGN_CAPITAL = '10000';
/**
 * Least capital a campaign mission must leave to carry on (1% of CAMPAIGN_CAPITAL). The cheapest
 * order in any campaign mission (0.1 contract near 100 cr, at a 2x cap: about 5 cr) costs far less.
 */
export const CAMPAIGN_MIN_CAPITAL = '100';
/** Ended campaign runs kept for the progress screen. */
const KEEP_HISTORY = 20;

export type LegResult = 'finished' | 'bankrupt' | 'left';

export interface CampaignLeg {
	runId: string;
	missionId: string;
	startCapital: string;
	endCapital: string;
	result: LegResult;
	/** A mission that reached its debrief: the engine debrief facts the campaign report reads (campaignReport.ts). */
	report?: LegReport;
}

export interface CampaignRun {
	id: string;
	startedAt: number;
	/** Capital the next mission starts with (decimal string). */
	capital: string;
	/** Index of the next mission in `order`. */
	index: number;
	status: 'active' | 'bankrupt' | 'complete' | 'abandoned';
	legs: CampaignLeg[];
	/**
	 * The run's missions, snapshotted when it started (progress v4): campaignMissions() then. A run
	 * stored before v4 reads with CAMPAIGN_ORDER_V3 (migrateProgress).
	 */
	order?: string[];
}

/** The missions of a campaign run, in order: its snapshot, else the order of the runs before v4. */
export function campaignOrder(c: Pick<CampaignRun, 'order'> | null | undefined): readonly string[] {
	return c?.order ?? CAMPAIGN_ORDER_V3;
}

/** The campaign part of the progress record. */
export interface CampaignProgress {
	campaign: CampaignRun | null;
	/** Ended campaign runs, oldest first. */
	campaignHistory: CampaignRun[];
	/** Missions finished in a campaign run: their curriculum stages are passed (never removed). */
	assessed: string[];
}

/** Links a run record to the campaign mission it plays. */
export interface CampaignLink {
	id: string;
	index: number;
}

export function newCampaign(id: string, now: number): CampaignRun {
	return { id, startedAt: now, capital: CAMPAIGN_CAPITAL, index: 0, status: 'active', legs: [], order: campaignMissions() };
}

/** The mission the active campaign run plays next, or null when there is none. */
export function nextCampaignMission(c: CampaignRun | null): string | null {
	if (!c || c.status !== 'active') return null;
	return campaignOrder(c)[c.index] ?? null;
}

/** Starts a new campaign run; an active one is ended as abandoned (kept in the history). */
export function startCampaign<P extends CampaignProgress>(p: P, id: string, now: number): P {
	const ended = p.campaign && p.campaign.status === 'active' ? { ...p.campaign, status: 'abandoned' as const } : p.campaign;
	return { ...p, campaign: newCampaign(id, now), campaignHistory: archive(p.campaignHistory, ended) };
}

/** Ends the active campaign run by the player's choice. Progress (assessed stages) is kept. */
export function abandonCampaign<P extends CampaignProgress>(p: P): P {
	if (!p.campaign || p.campaign.status !== 'active') return p;
	const ended = { ...p.campaign, status: 'abandoned' as const };
	return { ...p, campaign: ended, campaignHistory: archive(p.campaignHistory, ended) };
}

function archive(history: CampaignRun[], c: CampaignRun | null): CampaignRun[] {
	if (!c || c.status === 'active' || history.some((h) => h.id === c.id)) return history;
	return [...history, c].slice(-KEEP_HISTORY);
}

export interface RunOutcome {
	runId: string;
	missionId: string;
	result: LegResult;
	/** Equity when the run ended (engine debrief finalEquity) or was left (last known equity). */
	equity: string;
	/** The run's engine debrief facts, when it reached its debrief. */
	report?: LegReport;
}

/**
 * Applies a campaign mission's outcome once (idempotent by run id). Returns `p` unchanged when the
 * run does not belong to the active campaign run's current mission.
 */
export function settleCampaignRun<P extends CampaignProgress>(p: P, link: CampaignLink, out: RunOutcome): P {
	const c = p.campaign;
	if (!c || c.status !== 'active' || c.id !== link.id || c.index !== link.index) return p;
	if (c.legs.some((l) => l.runId === out.runId) || campaignOrder(c)[c.index] !== out.missionId) return p;
	const result = legResult(out);
	const leg: CampaignLeg = { runId: out.runId, missionId: out.missionId, startCapital: c.capital, endCapital: out.equity, result };
	if (out.report) leg.report = out.report;
	const next: CampaignRun = { ...c, capital: out.equity, legs: [...c.legs, leg], ...advance(c, result) };
	const assessed = result === 'finished' && !p.assessed.includes(out.missionId) ? [...p.assessed, out.missionId] : p.assessed;
	return { ...p, campaign: next, assessed, campaignHistory: archive(p.campaignHistory, next) };
}

/** Less than the campaign minimum left (finished or left) is a bankruptcy: the campaign cannot go on. */
export function belowCampaignMinimum(equity: string): boolean {
	return !(Number(equity) >= Number(CAMPAIGN_MIN_CAPITAL));
}

function legResult(out: RunOutcome): LegResult {
	return belowCampaignMinimum(out.equity) ? 'bankrupt' : out.result;
}

// Bankruptcy ends the run; a finished mission moves on; leaving stays.
function advance(c: CampaignRun, result: LegResult): Pick<CampaignRun, 'index' | 'status'> {
	if (result === 'bankrupt') return { index: c.index, status: 'bankrupt' };
	if (result === 'left') return { index: c.index, status: 'active' };
	const index = c.index + 1;
	return { index, status: index >= campaignOrder(c).length ? 'complete' : 'active' };
}

/** What a run outside the campaign tells the curriculum (design D14 "Elective stages"). */
export interface FreeRunOutcome {
	missionId: string;
	finished: boolean;
	/** Assisted practice never counts (rewind, another market, a mistake retry). */
	assisted: boolean;
	/** A campaign leg settles through settleCampaignRun instead. */
	campaign: boolean;
}

/**
 * A finished, non-assisted free-play or daily-challenge run of an elective stage's mission assesses
 * it (it is never a campaign leg), so the elective stage can pass. Idempotent; never removes anything.
 */
export function assessElective<P extends CampaignProgress>(p: P, out: FreeRunOutcome, elective: (missionId: string) => boolean): P {
	if (!out.finished || out.assisted || out.campaign || !elective(out.missionId) || p.assessed.includes(out.missionId)) return p;
	return { ...p, assessed: [...p.assessed, out.missionId] };
}

/** Best ending capital among ended campaign runs that completed every mission, or null. */
export function bestCompletedCapital(history: readonly CampaignRun[]): string | null {
	const done = history.filter((c) => c.status === 'complete');
	if (!done.length) return null;
	return done.reduce((best, c) => (Number(c.capital) > Number(best) ? c.capital : best), done[0].capital);
}
