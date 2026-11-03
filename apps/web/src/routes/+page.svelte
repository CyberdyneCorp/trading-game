<script lang="ts">
	import { onMount } from 'svelte';
	import { GameSession } from '#lib/game/session.svelte.ts';
	import { settings } from '#lib/game/settings.svelte.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import Briefing from '#lib/components/Briefing.svelte';
	import CrtOverlay from '#lib/components/CrtOverlay.svelte';
	import Debrief from '#lib/components/Debrief.svelte';
	import MissionMenu from '#lib/components/MissionMenu.svelte';
	import Terminal from '#lib/components/Terminal.svelte';

	const session = new GameSession();

	onMount(() => {
		settings.load();
		pwa.init();
		const seed = Number(new URLSearchParams(location.search).get('seed'));
		if (Number.isInteger(seed) && seed > 0) session.fixedSeed = seed;
		void session.init();
		return () => session.dispose();
	});
</script>

{#if session.error}
	<p class="panel down" role="alert">{t('state.error', { msg: session.error })}</p>
{:else if session.screen === 'loading'}
	<p class="loading">{t('state.loading')}</p>
{:else if session.screen === 'menu'}
	<MissionMenu {session} />
{:else if session.screen === 'briefing'}
	<Briefing {session} />
{:else if session.screen === 'terminal'}
	<Terminal {session} />
{:else if session.screen === 'debrief' && session.debrief}
	<Debrief {session} />
{/if}

{#if settings.crt}
	<CrtOverlay />
{/if}

<style>
	.loading {
		font-family: var(--font-pixel);
		font-size: 2rem;
		color: var(--accent);
		text-align: center;
		margin-top: 30vh;
	}
</style>
