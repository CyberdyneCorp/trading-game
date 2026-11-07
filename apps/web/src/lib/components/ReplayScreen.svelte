<script lang="ts">
	// Replay viewer (add-run-sharing-and-journal-stats design D6): an imported run, or one of the
	// player's own, rebuilt in the engine and stepped bar by bar. Read-only: no ticket, no command.
	import { onDestroy } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText, packOriginOf, packTextsOf } from '#lib/game/missions.ts';
	import { positionPlannedRisk } from '#lib/game/journal.ts';
	import Chart from './Chart.svelte';
	import RightRail from './RightRail.svelte';
	import EventFeed from './EventFeed.svelte';
	import JournalEntries from './JournalEntries.svelte';
	import RunExport from './RunExport.svelte';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const v = $derived(session.viewing!);
	const s = $derived(session.state!);
	const title = $derived(missionText(v.file.mission, L, packOriginOf(session.packs, v.file.pack), packTextsOf(session.packs, v.file.pack)).title);
	const notes = $derived(session.journal.filter((p) => p.step <= s.step));
	const ended = $derived(s.status !== 'active');
	let playing = $state(false);
	let timer: ReturnType<typeof setInterval> | null = null;

	function stop() {
		playing = false;
		if (timer) clearInterval(timer);
		timer = null;
	}
	function toggle() {
		if (playing) return stop();
		playing = true;
		timer = setInterval(() => {
			if (session.replayFinished) return stop();
			void session.replayStep();
		}, 350);
	}
	async function toEnd() {
		stop();
		await session.replayToEnd();
	}
	onDestroy(stop);
</script>

<main class="replay" data-testid="replay">
	<header class="panel">
		<h1>{t('replay.title')} · {title}</h1>
		<p class="muted">
			{#if v.kind === 'import'}<span class="tag attention">{t('runfile.tagImported')}</span> {t('replay.imported')}{:else}{t('replay.own')}{/if}
			{#if v.file.labels.campaign}<span class="tag info">{t('replay.labelCampaign')}</span>{/if}
			{#if v.file.labels.assisted}<span class="tag attention">{t('replay.labelAssisted')}</span>{/if}
			· seed {v.file.seed}
		</p>
		<div class="controls">
			<button onclick={() => session.replayStep()} disabled={session.busy || session.replayFinished} data-testid="replay-next">{t('replay.next')}</button>
			<button onclick={toggle} disabled={session.replayFinished} data-testid="replay-play">{playing ? t('replay.pause') : t('replay.play')}</button>
			<button onclick={toEnd} disabled={session.busy || session.replayFinished} data-testid="replay-end">{t('replay.end')}</button>
			{#if ended}<button class="primary" onclick={() => session.openReplayDebrief()} data-testid="replay-debrief">{t('replay.debrief')}</button>{/if}
			<button onclick={() => session.leaveReplay()} data-testid="replay-back">{t('replay.back')}</button>
			<span class="muted" role="status" data-testid="replay-bar">{t('replay.bar', { bar: v.bar, bars: v.bars })}</span>
		</div>
		{#if session.replayFinished && !ended}<p class="attention" data-testid="replay-end-active">{t('replay.endActive')}</p>{/if}
	</header>

	<div class="grid">
		<section class="panel chart"><Chart game={s} /></section>
		<aside class="panel">
			<h2 class="panel-title">{t('replay.account')}</h2>
			<RightRail game={s} plannedRisk={Number(s.position.qty) !== 0 ? positionPlannedRisk(session.journal) : null} />
		</aside>
	</div>
	<div class="grid two">
		<section class="panel">
			<h2 class="panel-title">{t('replay.events')}</h2>
			<EventFeed events={s.events} names={session.names} />
		</section>
		<section class="panel">
			<h2 class="panel-title">{t('replay.notes')}</h2>
			{#if v.file.journal.length === 0}<p class="muted">{t('replay.notesNone')}</p>{:else}<JournalEntries entries={notes} />{/if}
		</section>
	</div>
	<RunExport {session} />
</main>

<style>
	.replay {
		max-width: 1400px;
		margin: 0 auto;
		padding: 1rem;
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}
	h1 {
		margin: 0;
		font-size: 1.4rem;
	}
	.controls {
		display: flex;
		flex-wrap: wrap;
		gap: 0.5rem;
		align-items: center;
	}
	.grid {
		display: grid;
		grid-template-columns: minmax(0, 3fr) minmax(0, 1fr);
		gap: 0.8rem;
	}
	.grid.two {
		grid-template-columns: 1fr 1fr;
	}
	.chart {
		min-height: 420px;
	}
	@media (max-width: 800px) {
		.grid,
		.grid.two {
			grid-template-columns: 1fr;
		}
	}
</style>
