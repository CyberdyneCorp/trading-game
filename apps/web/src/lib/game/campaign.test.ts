import { describe, expect, it } from 'vitest';
import {
	abandonCampaign,
	belowCampaignMinimum,
	bestCompletedCapital,
	CAMPAIGN_CAPITAL,
	CAMPAIGN_MIN_CAPITAL,
	nextCampaignMission,
	settleCampaignRun,
	startCampaign,
	type CampaignProgress,
	type LegResult
} from './campaign.ts';
import { campaignMissions, stageStatus } from './curriculum.ts';

const EMPTY: CampaignProgress = { campaign: null, campaignHistory: [], assessed: [] };
const order = campaignMissions();

function play(p: CampaignProgress, runId: string, result: LegResult, equity: string): CampaignProgress {
	const c = p.campaign!;
	return settleCampaignRun(p, { id: c.id, index: c.index }, { runId, missionId: order[c.index], result, equity });
}

describe('campaign runs', () => {
	it('starts from the initial capital at the first curriculum mission (m06, design D8.4)', () => {
		const p = startCampaign(EMPTY, 'c1', 1);
		expect(p.campaign).toMatchObject({ id: 'c1', capital: CAMPAIGN_CAPITAL, index: 0, status: 'active', legs: [] });
		expect(nextCampaignMission(p.campaign)).toBe('m06-thin-book');
	});

	it('carries the final equity into the next mission and passes the finished mission', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		p = play(p, 'r1', 'finished', '9420.5');
		expect(p.campaign).toMatchObject({ capital: '9420.5', index: 1, status: 'active' });
		expect(p.campaign!.legs).toEqual([
			{ runId: 'r1', missionId: 'm06-thin-book', startCapital: '10000', endCapital: '9420.5', result: 'finished' }
		]);
		expect(nextCampaignMission(p.campaign)).toBe('m01-false-breakout');
		expect(p.assessed).toEqual(['m06-thin-book']);
	});

	it('settles a run once (a reload or a replayed debrief changes nothing)', () => {
		const p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '9000');
		const again = settleCampaignRun(p, { id: 'c1', index: 0 }, { runId: 'r1', missionId: 'm06-thin-book', result: 'finished', equity: '1' });
		expect(again).toBe(p);
	});

	it('ignores a run of another campaign run or of another mission index', () => {
		const p = startCampaign(EMPTY, 'c2', 1);
		const out = { runId: 'r', missionId: 'm06-thin-book', result: 'finished' as const, equity: '5' };
		expect(settleCampaignRun(p, { id: 'c1', index: 0 }, out)).toBe(p);
		expect(settleCampaignRun(p, { id: 'c2', index: 3 }, out)).toBe(p);
	});

	it('Bankrupt mid-campaign: bankruptcy ends the run; a new run starts from the initial capital, stages intact', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		p = play(p, 'r1', 'finished', '9500');
		p = play(p, 'r2', 'finished', '8000');
		p = play(p, 'r3', 'finished', '7000');
		expect(p.campaign!.index).toBe(3);
		p = play(p, 'r4', 'bankrupt', '3.2'); // mission 4 of the campaign run
		expect(p.campaign).toMatchObject({ status: 'bankrupt', index: 3 });
		expect(nextCampaignMission(p.campaign)).toBeNull();
		expect(p.campaignHistory.map((c) => c.id)).toEqual(['c1']);
		const passedBefore = ['structure', 'orders', 'futures1', 'futures2'].map((s) => stageStatus(s as never, p.assessed));
		expect(passedBefore).toEqual(['passed', 'passed', 'passed', 'passed']);

		const fresh = startCampaign(p, 'c2', 2);
		expect(fresh.campaign).toMatchObject({ id: 'c2', capital: CAMPAIGN_CAPITAL, index: 0, status: 'active' });
		expect(fresh.assessed).toEqual(p.assessed); // non-destructive failure
		expect(fresh.campaignHistory.map((c) => c.id)).toEqual(['c1']);
	});

	it('a liquidated (not bankrupt) mission still finishes: the campaign goes on with what is left', () => {
		const p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '6100');
		expect(p.campaign).toMatchObject({ status: 'active', capital: '6100', index: 1 });
	});

	it('ending with no equity left ends the campaign run like a bankruptcy', () => {
		const p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '0');
		expect(p.campaign!.status).toBe('bankrupt');
	});

	it('a finished mission that leaves less than the campaign minimum ends the run as bankrupt, stage not passed', () => {
		// Liquidated in m01: the engine still finishes the run, with too little left for the next mission.
		let p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '9000');
		p = play(p, 'r2', 'finished', '0.61');
		expect(p.campaign).toMatchObject({ status: 'bankrupt', index: 1, capital: '0.61' });
		expect(p.campaign!.legs[1].result).toBe('bankrupt');
		expect(p.assessed).toEqual(['m06-thin-book']);
		expect(nextCampaignMission(p.campaign)).toBeNull();
		expect(belowCampaignMinimum(CAMPAIGN_MIN_CAPITAL)).toBe(false);
		expect(belowCampaignMinimum('99.99')).toBe(true);
		const atMinimum = play(startCampaign(EMPTY, 'c2', 1), 'r3', 'finished', CAMPAIGN_MIN_CAPITAL);
		expect(atMinimum.campaign).toMatchObject({ status: 'active', index: 1 });
	});

	it('leaving a mission with less than the campaign minimum also ends the run', () => {
		const p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'left', '42');
		expect(p.campaign!.status).toBe('bankrupt');
	});

	it('a left mission carries the equity it had and must be played again', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		p = play(p, 'r1', 'left', '8700');
		expect(p.campaign).toMatchObject({ status: 'active', index: 0, capital: '8700' });
		expect(p.assessed).toEqual([]);
		expect(nextCampaignMission(p.campaign)).toBe('m06-thin-book');
	});

	it('completes after the last mission and records the best completed capital', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		order.forEach((_, i) => (p = play(p, `r${i}`, 'finished', String(10000 + i))));
		expect(p.campaign!.status).toBe('complete');
		expect(p.assessed).toEqual(order);
		expect(bestCompletedCapital(p.campaignHistory)).toBe(String(10000 + order.length - 1));
	});

	it('ending a campaign run by choice keeps it in the history and keeps passed stages', () => {
		let p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '9900');
		p = abandonCampaign(p);
		expect(p.campaign!.status).toBe('abandoned');
		expect(p.assessed).toEqual(['m06-thin-book']);
		expect(p.campaignHistory.map((c) => c.status)).toEqual(['abandoned']);
		expect(abandonCampaign(p)).toBe(p);
		// Starting a new run while one is active archives the active one as abandoned.
		const q = startCampaign(startCampaign(EMPTY, 'a', 1), 'b', 2);
		expect(q.campaignHistory.map((c) => [c.id, c.status])).toEqual([['a', 'abandoned']]);
	});
});

describe('campaign order snapshot and elective stages (progress v4, add-practice-and-advanced-strategies D14)', () => {
	it('a new campaign run snapshots the 21-mission order; a run without one plays the 17-mission order', async () => {
		const { campaignOrder, CAMPAIGN_ORDER_V3, newCampaign, nextCampaignMission } = await import('./campaign.ts');
		const { campaignMissions } = await import('./curriculum.ts');
		const fresh = newCampaign('c1', 0);
		expect(fresh.order).toEqual(campaignMissions());
		expect(fresh.order).toHaveLength(21);
		const old = { ...fresh, order: undefined, index: 5 };
		expect(campaignOrder(old)).toEqual(CAMPAIGN_ORDER_V3);
		expect(nextCampaignMission(old)).toBe('m04-options-buyer'); // index 5 of the 17: never m22
		expect(nextCampaignMission({ ...fresh, index: 5 })).toBe('m22-scale-manage');
	});

	it('an old run settles against its own order and completes after its 17th mission', async () => {
		const { CAMPAIGN_ORDER_V3, newCampaign, settleCampaignRun } = await import('./campaign.ts');
		const c = { ...newCampaign('c1', 0), order: [...CAMPAIGN_ORDER_V3], index: 16 };
		const p = { campaign: c, campaignHistory: [], assessed: [] };
		const out = settleCampaignRun(p, { id: 'c1', index: 16 }, { runId: 'r', missionId: 'm12-capstone', result: 'finished', equity: '12000' });
		expect(out.campaign?.status).toBe('complete');
	});

	it('a finished, non-assisted run outside the campaign assesses an elective mission, once; nothing else does', async () => {
		const { assessElective } = await import('./campaign.ts');
		const { isElectiveMission } = await import('./curriculum.ts');
		const p = { campaign: null, campaignHistory: [], assessed: ['m01-false-breakout'] };
		const base = { missionId: 'm18-gamma-scalp', finished: true, assisted: false, campaign: false };
		const once = assessElective(p, base, isElectiveMission);
		expect(once.assessed).toEqual(['m01-false-breakout', 'm18-gamma-scalp']);
		expect(assessElective(once, base, isElectiveMission)).toBe(once);
		expect(assessElective(p, { ...base, assisted: true }, isElectiveMission)).toBe(p);
		expect(assessElective(p, { ...base, finished: false }, isElectiveMission)).toBe(p);
		expect(assessElective(p, { ...base, campaign: true }, isElectiveMission)).toBe(p);
		expect(assessElective(p, { ...base, missionId: 'm22-scale-manage' }, isElectiveMission)).toBe(p); // a campaign mission
	});
});
