<script lang="ts">
	// Pre-mission tutorial (spec mission-tutorial): a few cards, keyboard friendly, skippable.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { tutorialFor } from '#lib/game/tutorials.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import TutorialArt from './TutorialArt.svelte';
	import RichText from './RichText.svelte';

	let { session }: { session: GameSession } = $props();

	const cards = $derived(tutorialFor(session.missionId, i18n.locale));
	const title = $derived(missionText(session.missionId, i18n.locale).title);
	let i = $state(0);
	const card = $derived(cards[Math.min(i, cards.length - 1)]);
	const last = $derived(i >= cards.length - 1);

	function next() {
		if (last) session.finishTutorial();
		else i += 1;
	}

	function onKeydown(e: KeyboardEvent) {
		// Enter on a focused button activates that button (Skip, Previous), not "Next".
		if (e.key === 'Enter' && e.target instanceof Element && e.target.closest('button')) return;
		if (e.key === 'ArrowRight' || e.key === 'Enter') {
			e.preventDefault();
			next();
		} else if (e.key === 'ArrowLeft' && i > 0) {
			i -= 1;
		} else if (e.key === 'Escape') {
			session.finishTutorial();
		}
	}
</script>

<svelte:window onkeydown={onKeydown} />

<main class="tutorial">
	<section class="panel" aria-labelledby="tut-title" aria-live="polite">
		<p class="muted head">
			<span>{t('tut.title')} · {title}</span>
			<span data-testid="tut-step">{t('tut.step', { n: i + 1, total: cards.length })}</span>
		</p>
		{#if card}
			<TutorialArt art={card.art} />
			<h1 id="tut-title">{card.title}</h1>
			<p class="body"><RichText text={card.body} /></p>
		{/if}
		<div class="dots" aria-hidden="true">
			{#each cards as _, k (k)}<span class:on={k === i}></span>{/each}
		</div>
		<div class="actions">
			<button class="link" onclick={() => session.finishTutorial()}>{t('tut.skip')}</button>
			<span class="nav">
				<button onclick={() => (i -= 1)} disabled={i === 0}>{t('tut.prev')}</button>
				<!-- svelte-ignore a11y_autofocus -->
				<button class="primary" onclick={next} autofocus>{last ? t('tut.done') : t('tut.next')}</button>
			</span>
		</div>
	</section>
</main>

<style>
	.tutorial {
		max-width: 620px;
		margin: 3rem auto;
		padding: 0 1rem;
	}
	.head {
		display: flex;
		justify-content: space-between;
		font-size: 0.8rem;
		margin: 0 0 0.8rem;
	}
	h1 {
		font-size: 1.3rem;
		margin: 0.9rem 0 0.4rem;
	}
	.body {
		min-height: 4.8em; /* keeps the buttons from jumping between cards */
		line-height: 1.5;
	}
	.dots {
		display: flex;
		gap: 0.35rem;
		justify-content: center;
		margin: 0.6rem 0;
	}
	.dots span {
		width: 0.5rem;
		height: 0.5rem;
		border: 1px solid var(--accent);
	}
	.dots span.on {
		background: var(--accent);
	}
	.actions {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 0.6rem;
		flex-wrap: wrap;
	}
	.nav {
		display: flex;
		gap: 0.4rem;
	}
	.link {
		background: none;
		border: none;
		color: var(--muted);
		text-decoration: underline;
	}
</style>
