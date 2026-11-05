// Campaign runs (spec gameplay-loop "Game over on bankruptcy"; add-scenarios-and-gameplay task 4.7).
//
// A campaign run plays the curriculum's missions in order. Each mission starts from the capital the
// previous one ended with (the engine's final equity, passed back as the `startingBalance` start
// option), so the engine stays the only source of balances. Bankruptcy ends the campaign run; a new
// one starts again from CAMPAIGN_CAPITAL. Finishing a mission in a campaign passes its curriculum
// stages for good (`assessed`): ending a campaign run never removes them.
// Leaving a campaign mission for another run carries the equity it had when it was left, and the
// campaign stays on that mission, so leaving never restores lost capital.

import { campaignMissions } from './curriculum.ts';

export const CAMPAIGN_CAPITAL = '10000';
/** Ended campaign runs kept for the progress screen. */
const KEEP_HISTORY = 20;

export type LegResult = 'finished' | 'bankrupt' | 'left';

export interface CampaignLeg {
	runId: string;
	missionId: string;
	startCapital: string;
	endCapital: string;
	result: LegResult;
}

export interface CampaignRun {
	id: string;
	startedAt: number;
	/** Capital the next mission starts with (decimal string). */
	capital: string;
	/** Index of the next mission in campaignMissions(). */
	index: number;
	status: 'active' | 'bankrupt' | 'complete' | 'abandoned';
	legs: CampaignLeg[];
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
	return { id, startedAt: now, capital: CAMPAIGN_CAPITAL, index: 0, status: 'active', legs: [] };
}

/** The mission the active campaign run plays next, or null when there is none. */
export function nextCampaignMission(c: CampaignRun | null): string | null {
	if (!c || c.status !== 'active') return null;
	return campaignMissions()[c.index] ?? null;
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
}

/**
 * Applies a campaign mission's outcome once (idempotent by run id). Returns `p` unchanged when the
 * run does not belong to the active campaign run's current mission.
 */
export function settleCampaignRun<P extends CampaignProgress>(p: P, link: CampaignLink, out: RunOutcome): P {
	const c = p.campaign;
	if (!c || c.status !== 'active' || c.id !== link.id || c.index !== link.index) return p;
	if (c.legs.some((l) => l.runId === out.runId) || campaignMissions()[c.index] !== out.missionId) return p;
	const leg: CampaignLeg = { runId: out.runId, missionId: out.missionId, startCapital: c.capital, endCapital: out.equity, result: out.result };
	const next: CampaignRun = { ...c, capital: out.equity, legs: [...c.legs, leg], ...advance(c, out) };
	const assessed = out.result === 'finished' && !p.assessed.includes(out.missionId) ? [...p.assessed, out.missionId] : p.assessed;
	return { ...p, campaign: next, assessed, campaignHistory: archive(p.campaignHistory, next) };
}

// Bankruptcy (or nothing left to trade with) ends the run; a finished mission moves on; leaving stays.
function advance(c: CampaignRun, out: RunOutcome): Pick<CampaignRun, 'index' | 'status'> {
	if (out.result === 'bankrupt' || !(Number(out.equity) > 0)) return { index: c.index, status: 'bankrupt' };
	if (out.result === 'left') return { index: c.index, status: 'active' };
	const index = c.index + 1;
	return { index, status: index >= campaignMissions().length ? 'complete' : 'active' };
}

/** Best ending capital among ended campaign runs that completed every mission, or null. */
export function bestCompletedCapital(history: readonly CampaignRun[]): string | null {
	const done = history.filter((c) => c.status === 'complete');
	if (!done.length) return null;
	return done.reduce((best, c) => (Number(c.capital) > Number(best) ? c.capital : best), done[0].capital);
}
