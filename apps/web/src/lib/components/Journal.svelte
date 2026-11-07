<script lang="ts">
	// Journal screen (web-client task 4.4): the plans recorded before each entry, run by run.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { journalMissions, journalRuns } from '#lib/game/journal.ts';
	import { missionText, packOriginOf, packTextsOf } from '#lib/game/missions.ts';
	import JournalEntries from './JournalEntries.svelte';
	import RunExport from './RunExport.svelte';
	import RunImport from './RunImport.svelte';
	import { importLabels } from '#lib/game/runSharing.svelte.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	let filter = $state('');
	const runs = $derived(journalRuns(session.journalRecords, filter || null));
	const missions = $derived(journalMissions(session.journalRecords));
	// A practice run is named by its pack's origin (catalog or imported), as on the briefing.
	function titleOf(missionId: string, digest: string | null | undefined = session.journalRecords.find((r) => r.missionId === missionId)?.packDigest) {
		return missionText(missionId, L, packOriginOf(session.packs, digest), packTextsOf(session.packs, digest)).title;
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
				{#each missions as m (m)}<option value={m}>{titleOf(m)}</option>{/each}
			</select>
		</label>
	{/if}

	{#if runs.length === 0}
		<p class="panel muted" data-testid="journal-empty">{t('journal.empty')}</p>
	{/if}
	{#each runs as run (run.runId)}
		<section class="panel run" data-testid="journal-run">
			<h2 class="panel-title">
				{titleOf(run.missionId, run.packDigest)}
				<span class="tag info">{t(`journal.mode.${run.mode}`)}</span>
				{#if run.intel}<span class="tag attention" data-testid="journal-intel">{t('journal.intel')}</span>{/if}
				{#if run.assisted}<span class="tag attention" data-testid="journal-assisted">{t('journal.assisted')}</span>{/if}
				<span class="muted small">{new Date(run.createdAt).toLocaleString(L)} · seed {run.seed} · {t(`journal.status.${run.status}`)}</span>
			</h2>
			<JournalEntries entries={run.entries} />
			<div class="run-actions">
				<button onclick={() => session.watchOwnRun(run.runId, 'journal')} data-testid="journal-watch">▶ {t('journal.watchOwn')}</button>
				<RunExport {session} runId={run.runId} compact />
			</div>
		</section>
	{/each}

	<section class="panel imports" data-testid="journal-imports">
		<h2 class="panel-title">{t('journal.imports')}</h2>
		<p class="muted small">{t('journal.importsNote')}</p>
		<RunImport {session} from="journal" />
		{#if session.sharing.imports.length === 0}
			<p class="muted" data-testid="journal-imports-empty">{t('journal.importsEmpty')}</p>
		{/if}
		<ul>
			{#each session.sharing.imports as rec (rec.id)}
				{@const labels = importLabels(rec)}
				<li data-testid="journal-import">
					<span class="tag attention">{t('runfile.tagImported')}</span>
					{titleOf(rec.mission, rec.pack)} · seed {rec.seed} · {t(`journal.status.${rec.status}`)}
					{#if labels.campaign}<span class="tag info">{t('replay.labelCampaign')}</span>{/if}
					{#if labels.assisted}<span class="tag attention">{t('replay.labelAssisted')}</span>{/if}
					<button class="primary" onclick={() => session.watchImport(rec.id, 'journal')} data-testid="journal-import-watch">▶ {t('runfile.watch')}</button>
					<button onclick={() => session.sharing.removeImport(rec.id)} data-testid="journal-import-remove">{t('runfile.remove')}</button>
				</li>
			{/each}
		</ul>
	</section>
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
	.run-actions {
		display: flex;
		flex-wrap: wrap;
		gap: 0.5rem;
		align-items: center;
		margin-top: 0.5rem;
	}
	.imports ul {
		list-style: none;
		padding: 0;
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	.imports li {
		display: flex;
		flex-wrap: wrap;
		gap: 0.5rem;
		align-items: center;
	}
</style>
