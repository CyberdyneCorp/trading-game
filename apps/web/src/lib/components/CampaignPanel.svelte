<script lang="ts">
	// Menu block of the campaign: the run in progress (missions done, current, capital) or a new one.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { bestCompletedCapital, CAMPAIGN_CAPITAL } from '#lib/game/campaign.ts';
	import { campaignMissions } from '#lib/game/curriculum.ts';
	import { fmtMoney } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const missions = campaignMissions();
	const c = $derived(session.progress.campaign);
	const next = $derived(session.campaignMission);
	const history = $derived(session.progress.campaignHistory);
	const best = $derived(bestCompletedCapital(history));

	function mark(i: number): string {
		if (!c) return '·';
		if (i < c.index || (c.status === 'complete' && i <= c.index)) return '✓';
		if (i === c.index && c.status === 'active') return '▶';
		if (i === c.index && c.status === 'bankrupt') return '☠';
		return '·';
	}

	function endedText(): string {
		if (!c) return '';
		if (c.status === 'complete') return t('campaign.ended.complete', { capital: fmtMoney(c.capital, L) });
		const capital = fmtMoney(CAMPAIGN_CAPITAL, L);
		return c.status === 'bankrupt' ? t('campaign.ended.bankrupt', { n: c.index + 1, capital }) : t('campaign.ended.abandoned', { capital });
	}
</script>

<section class="panel campaign" data-testid="campaign">
	<h2 class="panel-title">{t('campaign.title')}</h2>
	<p class="muted small">{t('campaign.note')}</p>
	<ol class="steps">
		{#each missions as m, i (m)}
			<li class:current={mark(i) === '▶'} class:done={mark(i) === '✓'}>{mark(i)} {missionText(m, L).title}</li>
		{/each}
	</ol>
	{#if c && next}
		<p data-testid="campaign-progress">
			{t('campaign.progress', { n: c.index + 1, total: missions.length, mission: missionText(next, L).title, capital: fmtMoney(c.capital, L) })}
		</p>
		<div class="buttons">
			<button class="primary" onclick={() => session.continueCampaign()} disabled={session.busy} data-testid="campaign-continue">
				▶ {t('campaign.continue')}
			</button>
			<button onclick={() => session.endCampaign()} disabled={session.busy}>{t('campaign.end')}</button>
		</div>
	{:else}
		{#if c}<p data-testid="campaign-ended">{endedText()}</p>{/if}
		<div class="buttons">
			<button class="primary" onclick={() => session.startCampaign()} disabled={session.busy} data-testid="campaign-start">
				▶ {t(c ? 'campaign.new' : 'campaign.start')}
			</button>
		</div>
	{/if}
	{#if history.length}
		<p class="muted small">
			{t('campaign.played', { n: history.length })}{#if best} · {t('campaign.best', { capital: fmtMoney(best, L) })}{/if}
		</p>
	{/if}
</section>

<style>
	.campaign {
		display: flex;
		flex-direction: column;
		gap: 0.45rem;
		margin: 1rem 0;
	}
	.steps {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-wrap: wrap;
		gap: 0.3rem 1rem;
	}
	.steps li.current {
		color: var(--accent);
		font-weight: bold;
	}
	.steps li.done {
		color: var(--up);
	}
	.buttons {
		display: flex;
		gap: 0.5rem;
	}
	.small {
		font-size: 0.8rem;
	}
	p {
		margin: 0;
	}
</style>
