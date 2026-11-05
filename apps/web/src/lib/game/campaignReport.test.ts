import { describe, expect, it } from 'vitest';
import type { EngineTrade } from '#lib/engine/types.ts';
import { campaignReport, legReport, REPORT_MIN_TRADES, type LegReport } from './campaignReport.ts';
import { newCampaign, settleCampaignRun, type CampaignProgress } from './campaign.ts';
import { campaignMissions } from './curriculum.ts';

const trade = (net: string, rBp: number | null, regime: string | null = 'range') => ({ net, rBp, regime });

function leg(trades: ReturnType<typeof trade>[], costs: Partial<LegReport> = {}): { report: LegReport } {
	const netPnl = String(trades.reduce((a, t) => a + Number(t.net), 0));
	return { report: { trades, netPnl, fees: '0', spreadSlippage: '0', funding: '0', ...costs } };
}

describe('campaign report', () => {
	it('Too few trades: 18 trades over the campaign show their numbers with the warning', () => {
		const wins = Array.from({ length: 8 }, () => trade('100', 10000));
		const losses = Array.from({ length: 10 }, () => trade('-50', -5000, 'trend'));
		const r = campaignReport([leg(wins.slice(0, 4)), leg([...wins.slice(4), ...losses])]);
		expect(r.trades).toBe(18);
		expect(r.tooFew).toBe(true);
		expect(r.missions).toBe(2);
		expect(r.wins).toBe(8);
		expect(r.winRate).toBeCloseTo(8 / 18);
		expect(r.avgWinR).toBeCloseTo(1);
		expect(r.avgLossR).toBeCloseTo(-0.5);
		expect(r.expectancyR).toBeCloseTo((8 * 1 - 10 * 0.5) / 18);
		expect(r.byRegime).toEqual([
			{ regime: 'trend', trades: 10, wins: 0, net: -500 },
			{ regime: 'range', trades: 8, wins: 8, net: 800 }
		]);
	});

	it('30 trades or more is a usable sample; trades without a stop stay out of the R averages', () => {
		const trades = [...Array.from({ length: REPORT_MIN_TRADES - 1 }, () => trade('10', 2000)), trade('-40', null, null)];
		const r = campaignReport([leg(trades)]);
		expect(r.tooFew).toBe(false);
		expect(r.withoutR).toBe(1);
		expect(r.expectancyR).toBeCloseTo(0.2);
		expect(r.byRegime.find((x) => x.regime === 'unlabeled')?.trades).toBe(1);
	});

	it('costs are fees, spread and slippage and funding paid, as a share of gross P&L (net + costs)', () => {
		const r = campaignReport([leg([trade('300', 10000)], { fees: '20', spreadSlippage: '30', funding: '-50' }), leg([], { funding: '25' })]);
		expect(r.costs).toBe(100); // funding received is not a cost
		expect(r.gross).toBe(400);
		expect(r.costShare).toBeCloseTo(0.25);
		const losing = campaignReport([leg([trade('-300', -10000)], { fees: '20' })]);
		expect(losing.costShare).toBeNull(); // gross P&L negative: no share
	});

	it('legs without a stored debrief (left missions) are skipped', () => {
		const r = campaignReport([{}, leg([trade('5', 1000)])]);
		expect(r.missions).toBe(1);
		expect(r.trades).toBe(1);
		expect(campaignReport([]).winRate).toBeNull();
	});

	it("legReport copies the engine's numbers and never recomputes them", () => {
		const tr = { net: '12.5', rBp: 2500, regime: 'high_vol' } as EngineTrade;
		const r = legReport({ trades: [tr], netPnl: '10', spreadSlippage: '1.5' }, { feesPaid: '2', fundingNet: '-0.5' });
		expect(r).toEqual({ trades: [{ net: '12.5', rBp: 2500, regime: 'high_vol' }], netPnl: '10', fees: '2', spreadSlippage: '1.5', funding: '-0.5' });
	});

	it('a settled campaign mission keeps its report on its leg', () => {
		const p: CampaignProgress = { campaign: newCampaign('c1', 0), campaignHistory: [], assessed: [] };
		const report = leg([trade('10', 1000)]).report;
		const next = settleCampaignRun(p, { id: 'c1', index: 0 }, { runId: 'r1', missionId: campaignMissions()[0], result: 'finished', equity: '10010', report });
		expect(next.campaign?.legs[0].report).toEqual(report);
		const left = settleCampaignRun(next, { id: 'c1', index: 1 }, { runId: 'r2', missionId: campaignMissions()[1], result: 'left', equity: '9000' });
		expect(left.campaign?.legs[1].report).toBeUndefined();
	});
});
