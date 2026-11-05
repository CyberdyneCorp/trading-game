<script lang="ts">
	// Journal screen (web-client task 4.4): the plans recorded before each entry, run by run.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { journalMissions, journalRuns } from '#lib/game/journal.ts';
	import { missionText } from '#lib/game/missions.ts';
	import JournalEntries from './JournalEntries.svelte';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	let filter = $state('');
	const runs = $derived(journalRuns(session.journalRecords, filter || null));
	const missions = $derived(journalMissions(session.journalRecords));
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
			<JournalEntries entries={run.entries} />
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
	.small {
		font-size: 0.8rem;
	}
</style>
