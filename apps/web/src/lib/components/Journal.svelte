<script lang="ts">
	// Journal screen (web-client task 4.4): the plans recorded before each entry, run by run.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import { fmtMoney, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { journalMissions, journalRuns } from '#lib/game/journal.ts';
	import { missionText } from '#lib/game/missions.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	let filter = $state('');
	const runs = $derived(journalRuns(session.journalRecords, filter || null));
	const missions = $derived(journalMissions(session.journalRecords));

	// Old entries may hold NaN (a decimal comma read before the fix): shown as no level.
	const level = (v: number | null) => (v != null && Number.isFinite(v) && v > 0 ? fmtPrice(String(v), L) : null);

	function details(p: PlanEntry): string[] {
		const out = [t('journal.conf', { v: p.confidence })];
		const inv = level(p.invalidation);
		const target = level(p.target);
		if (inv) out.push(t('journal.inv', { v: inv }));
		if (target) out.push(t('journal.target', { v: target }));
		if (p.plannedRisk != null && p.plannedRisk > 0) out.push(t('journal.risk', { v: fmtMoney(p.plannedRisk, L) }));
		return out;
	}
</script>

<main class="journal">
	<header>
		<h1>{t('journal.title')}</h1>
		<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
	</header>
	<p class="muted">{t('journal.note')}</p>

	{#if missions.length > 1}
		<label class="filter">
			{t('journal.filter')}
			<select bind:value={filter} data-testid="journal-filter">
				<option value="">{t('journal.all')}</option>
				{#each missions as m (m)}<option value={m}>{missionText(m, L).title}</option>{/each}
			</select>
		</label>
	{/if}

	{#if runs.length === 0}
		<p class="panel muted" data-testid="journal-empty">{t('journal.empty')}</p>
	{/if}
	{#each runs as run (run.runId)}
		<section class="panel run" data-testid="journal-run">
			<h2 class="panel-title">
				{missionText(run.missionId, L).title}
				<span class="tag info">{t(`journal.mode.${run.mode}`)}</span>
				{#if run.intel}<span class="tag attention" data-testid="journal-intel">{t('journal.intel')}</span>{/if}
				{#if run.assisted}<span class="tag attention" data-testid="journal-assisted">{t('journal.assisted')}</span>{/if}
				<span class="muted small">{new Date(run.createdAt).toLocaleString(L)} · seed {run.seed} · {t(`journal.status.${run.status}`)}</span>
			</h2>
			<ol class="entries">
				{#each run.entries as p, i (i)}
					<li data-testid="journal-entry">
						<span class="muted">[{t('journal.bar', { step: p.step })}]</span>
						<span class={p.side === 'buy' ? 'up' : 'down'}>{p.side === 'buy' ? '▲ LONG' : '▼ SHORT'}{p.instrument ? ` · ${p.instrument}` : ''}</span>
						{#if p.skipped}
							<span class="muted">— {t('journal.skipped')}</span>
						{:else}
							<span class="thesis">{p.thesis}</span>
							<span class="muted small">{details(p).join(' · ')}</span>
							{#if p.loseScenario}<span class="muted small">{t('journal.lose', { v: p.loseScenario })}</span>{/if}
						{/if}
					</li>
				{/each}
			</ol>
		</section>
	{/each}
</main>

<style>
	.journal {
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
	h2 {
		display: flex;
		flex-wrap: wrap;
		gap: 0.5rem;
		align-items: baseline;
	}
	.filter {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}
	.filter select {
		width: auto;
	}
	.entries {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 0.35rem;
	}
	.entries li {
		display: flex;
		flex-wrap: wrap;
		gap: 0.2rem 0.6rem;
		align-items: baseline;
	}
	.thesis {
		font-style: italic;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
