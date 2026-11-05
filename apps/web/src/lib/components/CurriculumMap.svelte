<script lang="ts">
	// Curriculum map and progress (spec curriculum; web-client task 4.4): every stage in dependency
	// order with its status, assessment missions and unlocks, plus the player's progress.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { bestCompletedCapital } from '#lib/game/campaign.ts';
	import { curriculumSummary, LOCKED_LEVERAGE_CAP, STAGES, stageStatus, type Unlock } from '#lib/game/curriculum.ts';
	import { fmtMoney } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const assessed = $derived(session.progress.assessed);
	const summary = $derived(curriculumSummary(assessed));
	const best = $derived(bestCompletedCapital(session.progress.campaignHistory));
	const unlockText = (u: Unlock) => t(`unlock.${u}`, { cap: LOCKED_LEVERAGE_CAP });
	const missionsText = (ids: string[]) =>
		ids.map((m) => `${assessed.includes(m) ? '✓ ' : ''}${missionText(m, L).title}`).join(', ');
</script>

<main class="curriculum">
	<header>
		<h1>{t('curriculum.title')}</h1>
		<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
	</header>
	<p class="muted">{t('curriculum.note')}</p>

	<section class="panel progress" data-testid="progress">
		<h2 class="panel-title">{t('curriculum.progress')}</h2>
		<p data-testid="curriculum-summary">{t('curriculum.summary', { passed: summary.passed, total: summary.withContent })}</p>
		<p>{t('curriculum.completed', { n: session.completed.length, total: session.missions.length })}</p>
		<p>
			{t('campaign.played', { n: session.progress.campaignHistory.length })}{#if best}{' · '}{t('campaign.best', { capital: fmtMoney(best, L) })}{/if}
		</p>
	</section>

	<ol class="stages">
		{#each STAGES as st, i (st.id)}
			{@const status = stageStatus(st.id, assessed)}
			<li class="panel stage {status}" data-testid="stage" data-stage={st.id} data-status={status}>
				<div class="head">
					<span class="idx">{i + 1}</span>
					<strong>{t(`stage.${st.id}`)}</strong>
					<span class="tag status">{t(`curriculum.status.${status}`)}</span>
				</div>
				{#if st.missions.length}
					<p>{t('curriculum.assessment', { missions: missionsText(st.missions) })}</p>
				{:else}
					<p class="muted small">{t('curriculum.soonNote')}</p>
				{/if}
				{#if st.unlocks.length}
					<p class="info">{t('curriculum.unlocks', { what: st.unlocks.map(unlockText).join(', ') })}</p>
				{/if}
			</li>
		{/each}
	</ol>
</main>

<style>
	.curriculum {
		max-width: 900px;
		margin: 0 auto;
		padding: 2rem 1rem;
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 1rem;
	}
	h1 {
		margin: 0;
	}
	.progress p,
	.stage p {
		margin: 0.2rem 0 0;
	}
	.stages {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	.head {
		display: flex;
		align-items: baseline;
		gap: 0.6rem;
	}
	.idx {
		font-family: var(--font-pixel);
		color: var(--muted);
		min-width: 1.6rem;
	}
	.status {
		margin-left: auto;
	}
	.stage.passed {
		border-color: var(--up);
	}
	.stage.passed .status {
		color: var(--up);
	}
	.stage.open {
		border-color: var(--accent);
	}
	.stage.locked .status {
		color: var(--attention);
	}
	.stage.soon {
		opacity: 0.75;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
