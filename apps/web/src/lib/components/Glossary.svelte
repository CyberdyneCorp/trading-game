<script lang="ts">
	// Glossary screen, from the menu and the Academy (glossary spec "Glossary screen").
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import GlossaryList from './GlossaryList.svelte';

	let { session }: { session: GameSession } = $props();
</script>

<main class="glossary-screen">
	<header>
		<h1>{t('glossary.title')}</h1>
		<button onclick={() => session.leaveGlossary()} data-testid="glossary-back">{t('glossary.back')}</button>
	</header>
	<p class="muted">{t('glossary.note')}</p>
	<section class="panel">
		<GlossaryList seen={session.progress.glossarySeen} onOpen={(id) => session.markGlossarySeen(id)} />
	</section>
</main>

<style>
	.glossary-screen {
		max-width: 900px;
		margin: 0 auto;
		padding: 2rem 1rem;
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
</style>
