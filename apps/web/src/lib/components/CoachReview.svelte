<script lang="ts">
	// Coach mode (add-practice-and-advanced-strategies design D17): the engine's moments of the finished
	// run, one at a time. Each moment's chart is the run cut at its bar (stateAt): nothing after it is
	// drawn, so the player judges with what was visible. Warnings offer "Retry from here" (design D18).
	// The reveal is linked only after the last moment.
	import { onMount } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { momentText, retryable, stateAt } from '#lib/game/coachReview.ts';
	import { lessonMeta } from '#lib/content/academy.ts';
	import { termById } from '#lib/content/glossary.ts';
	import { glossaryUi } from '#lib/game/glossaryUi.svelte.ts';
	import Chart from './Chart.svelte';

	let { session, onClose, onReveal }: { session: GameSession; onClose: () => void; onReveal: () => void } = $props();
	const L = $derived(i18n.locale);
	let index = $state(0);

	onMount(() => void session.loadCoach());

	const moments = $derived(session.coach?.review?.moments ?? []);
	const atEnd = $derived(index >= moments.length);
	const m = $derived(atEnd ? null : moments[index]);
	const cut = $derived(m && session.state ? stateAt(session.state, m.step) : null);
	const lesson = $derived(m ? lessonMeta(m.lesson) : null);
	const term = $derived(m ? termById(m.glossary) : null);

	function onKey(e: KeyboardEvent) {
		if (e.key === 'Escape') onClose();
	}
</script>

<svelte:window onkeydown={onKey} />

<div class="modal-backdrop" role="presentation">
	<div class="modal coach" role="dialog" aria-modal="true" aria-labelledby="coach-title" data-testid="coach-review">
		<header class="row">
			<h2 id="coach-title">{t('coachReview.title')}</h2>
			<button onclick={onClose} data-testid="coach-close">{t('coachReview.close')}</button>
		</header>
		<p class="muted small">{t('coachReview.intro')}</p>
		{#if !session.coach}
			<p role="status">{t('coachReview.loading')}</p>
		{:else if session.coach.error}
			<p class="attention" role="status">{t('coachReview.unavailable', { reason: session.coach.error })}</p>
		{:else if moments.length === 0}
			<p data-testid="coach-none">{t('coachReview.none')}</p>
			<button onclick={onReveal} data-testid="coach-reveal">{t('coachReview.toReveal')}</button>
		{:else if m && cut}
			<p class="row small">
				<span data-testid="coach-position">{t('coachReview.position', { n: index + 1, total: moments.length, step: m.step })}</span>
				<span class="tag {m.tone === 'good' ? 'up' : 'attention'}" data-testid="coach-tone">{m.tone === 'good' ? t('coachReview.good') : t('coachReview.warn')}</span>
			</p>
			<div class="chart-box" data-testid="coach-chart" data-last-bar={cut.bars.length - 1}>
				<Chart game={cut} moment={m.step} />
			</div>
			<p class="text" data-testid="coach-text" aria-live="polite">{momentText(m, L)}</p>
			<p class="small">
				{#if lesson}<button class="link" onclick={() => session.openLesson(m.lesson, 'debrief')} data-testid="coach-lesson">🎓 {t('coachReview.lesson', { title: lesson.title[L] })}</button>{/if}
				{#if term}<button class="link" onclick={() => glossaryUi.openAt(m.glossary)} data-testid="coach-term">📖 {t('coachReview.term', { term: term.text[L].term })}</button>{/if}
			</p>
			<div class="row actions">
				<button onclick={() => (index = Math.max(0, index - 1))} disabled={index === 0} data-testid="coach-prev">{t('coachReview.prev')}</button>
				{#if retryable(m) && session.mistakeRetryAllowed}
					<button onclick={() => session.retryMistake(m.step, m.code)} disabled={session.busy} data-testid="coach-retry">↺ {t('coachReview.retry')}</button>
				{/if}
				<button class="primary" onclick={() => (index += 1)} data-testid="coach-next">{t('coachReview.next')}</button>
			</div>
		{:else}
			<p data-testid="coach-end">{t('coachReview.end')}</p>
			<div class="row actions">
				<button onclick={() => (index = Math.max(0, moments.length - 1))}>{t('coachReview.prev')}</button>
				<button class="primary" onclick={onReveal} data-testid="coach-reveal">{t('coachReview.toReveal')}</button>
			</div>
		{/if}
	</div>
</div>

<style>
	.coach {
		width: min(60rem, 96vw);
		max-height: 94vh;
		overflow: auto;
	}
	.row {
		display: flex;
		gap: 0.75rem;
		justify-content: space-between;
		align-items: center;
		flex-wrap: wrap;
	}
	.chart-box {
		height: 18rem;
	}
	.text {
		line-height: 1.5;
	}
	.actions {
		justify-content: flex-end;
	}
</style>
