<script lang="ts">
	// The capstone's campaign report (spec gameplay-loop "Campaign report"): trades, win rate, average
	// R, expectancy in R, the cost share of gross P&L and results by revealed regime, over the settled
	// missions of this campaign run, from their stored engine debriefs. Below REPORT_MIN_TRADES it says
	// the sample is too small to conclude.
	import type { CampaignLeg } from '#lib/game/campaign.ts';
	import { campaignReport } from '#lib/game/campaignReport.ts';
	import { regimeName } from '#lib/game/debrief.ts';
	import { fmtMoney, fmtNum, fmtPct } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { legs }: { legs: CampaignLeg[] } = $props();
	const L = $derived(i18n.locale);
	const r = $derived(campaignReport(legs));
	const fmtR = (v: number | null) => (v == null ? '—' : `${fmtNum(v, L, 2, true)}R`);
</script>

<section class="panel report" data-testid="campaign-report">
	<h2 class="panel-title">{t('report.title')}</h2>
	{#if r.missions === 0}
		<p class="muted">{t('report.none')}</p>
	{:else}
		<p class="muted small">{t('report.missions', { n: r.missions })}</p>
		<p class={r.tooFew ? 'attention' : 'info'} data-testid="report-sample">
			{r.tooFew ? '⚠' : 'ℹ'} {t(r.tooFew ? 'report.tooFew' : 'report.enough', { n: r.trades })}
		</p>
		<dl class="kv">
			<dt>{t('report.trades')}</dt><dd data-testid="report-trades">{r.trades}</dd>
			<dt>{t('report.winRate')}</dt><dd>{r.winRate == null ? '—' : fmtPct(r.winRate, L, 0)}</dd>
			<dt>{t('report.avgWin')}</dt><dd class="up">{fmtR(r.avgWinR)}</dd>
			<dt>{t('report.avgLoss')}</dt><dd class="down">{fmtR(r.avgLossR)}</dd>
			<dt>{t('report.expectancy')}</dt><dd data-testid="report-expectancy">{fmtR(r.expectancyR)}</dd>
			<dt>{t('report.costs')}</dt><dd>{fmtMoney(r.costs, L)}</dd>
			<dt>{t('report.costShare')}</dt>
			<dd data-testid="report-cost-share">{r.costShare == null ? t('report.grossNegative') : fmtPct(r.costShare, L, 1)}</dd>
		</dl>
		{#if r.withoutR > 0}<p class="muted small">{t('report.noR')}</p>{/if}
		{#if r.byRegime.length}
			<h3>{t('report.byRegime')}</h3>
			<table class="regimes">
				<thead>
					<tr>
						<th>{t('report.col.regime')}</th><th class="num">{t('report.col.trades')}</th><th class="num">{t('report.col.wins')}</th>
						<th class="num">{t('report.col.net')}</th>
					</tr>
				</thead>
				<tbody>
					{#each r.byRegime as row (row.regime)}
						<tr>
							<td>{regimeName(row.regime as Parameters<typeof regimeName>[0])}</td>
							<td class="num">{row.trades}</td>
							<td class="num">{row.wins}</td>
							<td class="num {row.net >= 0 ? 'up' : 'down'}">{fmtMoney(row.net, L, true)}</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{/if}
	{/if}
</section>

<style>
	.report {
		border-color: var(--info);
	}
	.small {
		font-size: 0.8rem;
	}
	.regimes {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
	}
	.regimes th,
	.regimes td {
		padding: 0.2rem 0.4rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.regimes .num {
		text-align: right;
	}
</style>
