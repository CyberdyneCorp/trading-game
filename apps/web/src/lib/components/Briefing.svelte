<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { campaignMissions, campaignPermissions, LOCKED_LEVERAGE_CAP, unlockStage } from '#lib/game/curriculum.ts';
	import { tutorialFor } from '#lib/game/tutorials.ts';
	import PerkPanel from './PerkPanel.svelte';
	import { fmtMoney, fmtNum, fmtPct, num } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { session }: { session: GameSession } = $props();
	const s = $derived(session.state!);
	const L = $derived(i18n.locale);
	const c = $derived(s.contract);
	const txt = $derived(missionText(s.mission, L));
	// Higher maintenance tiers by notional (policy v16): "≥ 50.000: 1% (20x)".
	const tiers = $derived(
		(c.marginTiers ?? []).slice(1).map((m) =>
			t('brief.tier', { floor: fmtNum(num(m.notionalFloor), L, 0), mmr: fmtPct(num(m.maintenanceMargin), L, 1), max: m.maxLeverage })
		)
	);
	const link = $derived(session.runCampaign);
	const modeText = $derived.by(() => {
		if (link) return t(link.index === 0 ? 'brief.modeCampaignFirst' : 'brief.modeCampaign', { n: link.index + 1, total: campaignMissions().length, seed: session.seed });
		return s.knownScenario ? t('brief.modePack', { seed: session.seed }) : t('brief.modeSynthetic', { seed: session.seed });
	});
	// What the curriculum keeps locked in this campaign mission (option unlocks only where options
	// trade), and the stage that unlocks it.
	const locked = $derived(
		link
			? campaignPermissions(s.mission, session.progress.assessed).locked.filter((u) => s.options.enabled || !u.startsWith('options')).map((u) =>
					t('unlock.after', { what: t(`unlock.${u}`, { cap: LOCKED_LEVERAGE_CAP }), stage: t(`stage.${unlockStage(u)}`) })
				)
			: []
	);
</script>

<main class="brief">
	<div class="panel">
		<p class="muted">{t('brief.title')} · {s.alias}</p>
		<h1>{txt.title}</h1>
		<p class="tagline">{txt.tagline}</p>
		<dl class="kv">
			<dt>{t('brief.mode')}</dt><dd class="attention">{modeText}</dd>
			<dt>{t('brief.capital')}</dt><dd>{fmtMoney(s.initialBalance, L)}</dd>
			<dt>{t('brief.instrument')}</dt><dd>{t(c.allowShort ? 'brief.instrumentValue' : 'brief.instrumentValueLong')}</dd>
			<dt>{t('brief.leverage')}</dt><dd>1x – {c.leverageCap}x</dd>
			<dt>{t('brief.mmr')}</dt><dd>{fmtPct(num(c.maintenanceMargin), L)}</dd>
			{#if tiers.length}<dt>{t('brief.tiers')}</dt><dd data-testid="brief-tiers">{tiers.join(' · ')}</dd>{/if}
			{#if c.markFromLastPrice != null}
				<dt>{t('brief.mark')}</dt><dd data-testid="brief-mark">{t(c.markFromLastPrice ? 'brief.markLast' : 'brief.markSeries')}</dd>
			{/if}
			<dt>{t('brief.costs')}</dt>
			<dd>
				{t('brief.costsValue', {
					taker: fmtPct(num(c.takerFee), L, 3),
					maker: fmtPct(num(c.makerFee), L, 3),
					slip: fmtPct(num(c.halfSpread) + num(c.slippage), L, 3),
					funding: c.fundingInterval
				})}
			</dd>
		</dl>
		<h3>{t('brief.objectives')}</h3>
		<ul>
			{#each txt.objectives as o (o)}<li>{o}</li>{/each}
		</ul>
		{#if locked.length}
			<p class="locked" data-testid="campaign-locked">🔒 {t('campaign.locked', { what: locked.join(', ') })}</p>
		{/if}
		<PerkPanel {session} />
		<p class="rules">{t('brief.rules')}</p>
		<p class="muted small">{txt.topic}</p>
		<div class="actions">
			<span class="left">
				<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
				{#if tutorialFor(s.mission, L).length}<button onclick={() => session.showTutorial()}>? {t('brief.tutorial')}</button>{/if}
			</span>
			<!-- svelte-ignore a11y_autofocus -->
			<button class="primary" onclick={() => session.enterTerminal()} autofocus>▶ {t('brief.go')}</button>
		</div>
	</div>
</main>

<style>
	.brief {
		max-width: 760px;
		margin: 3rem auto;
		padding: 0 1rem;
	}
	.tagline {
		font-size: 1.05rem;
	}
	.kv {
		margin: 1rem 0;
	}
	.locked {
		border-left: 3px solid var(--attention);
		padding-left: 0.7rem;
		color: var(--attention);
	}
	.rules {
		border-left: 3px solid var(--info);
		padding-left: 0.7rem;
		color: var(--info);
	}
	.actions {
		display: flex;
		justify-content: space-between;
		margin-top: 1rem;
	}
	.left {
		display: flex;
		gap: 0.4rem;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
