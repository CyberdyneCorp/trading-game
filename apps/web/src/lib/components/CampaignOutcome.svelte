<script lang="ts">
	// Debrief block of a campaign mission: the capital it carries on, or the end of the campaign run.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { CAMPAIGN_CAPITAL } from '#lib/game/campaign.ts';
	import { campaignMissions, stagesOf } from '#lib/game/curriculum.ts';
	import { fmtMoney } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const total = campaignMissions().length;
	const link = $derived(session.runCampaign);
	const c = $derived(session.progress.campaign);
	const leg = $derived(c && link && c.id === link.id ? c.legs.find((l) => l.runId === session.currentRunId) : undefined);
	const next = $derived(session.campaignMission);
	const stages = $derived(leg?.result === 'finished' ? stagesOf(leg.missionId).map((s) => t(`stage.${s}`)).join(', ') : '');
</script>

{#if link && c && leg}
	<section class="panel campaign" data-testid="campaign-outcome">
		<h2 class="panel-title">{t('campaign.tag', { n: link.index + 1, total })}</h2>
		<p>{t('campaign.leg', { start: fmtMoney(leg.startCapital, L), end: fmtMoney(leg.endCapital, L) })}</p>
		{#if stages}<p class="up">✓ {t('campaign.passed', { stages })}</p>{/if}
		{#if c.status === 'active' && next}
			<p>{t('campaign.carry', { capital: fmtMoney(c.capital, L), mission: missionText(next, L).title })}</p>
			<button class="primary" onclick={() => session.startCampaignMission()} disabled={session.busy}>▶ {t('campaign.next')}</button>
		{:else if c.status === 'complete'}
			<p class="up">{t('campaign.complete', { capital: fmtMoney(c.capital, L) })}</p>
			<button class="primary" onclick={() => session.startCampaign()} disabled={session.busy}>▶ {t('campaign.new')}</button>
		{:else}
			<p class="down">{t('campaign.over', { capital: fmtMoney(CAMPAIGN_CAPITAL, L) })}</p>
			<button class="primary" onclick={() => session.startCampaign()} disabled={session.busy}>▶ {t('campaign.new')}</button>
		{/if}
	</section>
{/if}

<style>
	.campaign {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
		align-items: flex-start;
		border-color: var(--info);
	}
	p {
		margin: 0;
	}
</style>
