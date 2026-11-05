import { describe, expect, it } from 'vitest';
import {
	abandonCampaign,
	bestCompletedCapital,
	CAMPAIGN_CAPITAL,
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
	it('starts from the initial capital at the first curriculum mission', () => {
		const p = startCampaign(EMPTY, 'c1', 1);
		expect(p.campaign).toMatchObject({ id: 'c1', capital: CAMPAIGN_CAPITAL, index: 0, status: 'active', legs: [] });
		expect(nextCampaignMission(p.campaign)).toBe('m01-false-breakout');
	});

	it('carries the final equity into the next mission and passes the finished mission', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		p = play(p, 'r1', 'finished', '9420.5');
		expect(p.campaign).toMatchObject({ capital: '9420.5', index: 1, status: 'active' });
		expect(p.campaign!.legs).toEqual([
			{ runId: 'r1', missionId: 'm01-false-breakout', startCapital: '10000', endCapital: '9420.5', result: 'finished' }
		]);
		expect(nextCampaignMission(p.campaign)).toBe('m03-volatility-shock');
		expect(p.assessed).toEqual(['m01-false-breakout']);
	});

	it('settles a run once (a reload or a replayed debrief changes nothing)', () => {
		const p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '9000');
		const again = settleCampaignRun(p, { id: 'c1', index: 0 }, { runId: 'r1', missionId: 'm01-false-breakout', result: 'finished', equity: '1' });
		expect(again).toBe(p);
	});

	it('ignores a run of another campaign run or of another mission index', () => {
		const p = startCampaign(EMPTY, 'c2', 1);
		const out = { runId: 'r', missionId: 'm01-false-breakout', result: 'finished' as const, equity: '5' };
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
		const passedBefore = ['futures1', 'futures2', 'futures3'].map((s) => stageStatus(s as never, p.assessed));
		expect(passedBefore).toEqual(['passed', 'passed', 'passed']);

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

	it('a left mission carries the equity it had and must be played again', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		p = play(p, 'r1', 'left', '8700');
		expect(p.campaign).toMatchObject({ status: 'active', index: 0, capital: '8700' });
		expect(p.assessed).toEqual([]);
		expect(nextCampaignMission(p.campaign)).toBe('m01-false-breakout');
	});

	it('completes after the last mission and records the best completed capital', () => {
		let p = startCampaign(EMPTY, 'c1', 1);
		order.forEach((_, i) => (p = play(p, `r${i}`, 'finished', String(10000 + i))));
		expect(p.campaign!.status).toBe('complete');
		expect(p.assessed).toEqual(order);
		expect(bestCompletedCapital(p.campaignHistory)).toBe('10004');
	});

	it('ending a campaign run by choice keeps it in the history and keeps passed stages', () => {
		let p = play(startCampaign(EMPTY, 'c1', 1), 'r1', 'finished', '9900');
		p = abandonCampaign(p);
		expect(p.campaign!.status).toBe('abandoned');
		expect(p.assessed).toEqual(['m01-false-breakout']);
		expect(p.campaignHistory.map((c) => c.status)).toEqual(['abandoned']);
		expect(abandonCampaign(p)).toBe(p);
		// Starting a new run while one is active archives the active one as abandoned.
		const q = startCampaign(startCampaign(EMPTY, 'a', 1), 'b', 2);
		expect(q.campaignHistory.map((c) => [c.id, c.status])).toEqual([['a', 'abandoned']]);
	});
});
