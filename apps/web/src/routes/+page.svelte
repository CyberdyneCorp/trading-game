<script lang="ts">
	import { onMount, untrack } from 'svelte';
	import { GameSession } from '#lib/game/session.svelte.ts';
	import { settings } from '#lib/game/settings.svelte.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import { sound } from '#lib/game/sound.svelte.ts';
	import { moodFor, StingTracker } from '#lib/game/audioRules.ts';
	import Briefing from '#lib/components/Briefing.svelte';
	import CrtOverlay from '#lib/components/CrtOverlay.svelte';
	import Debrief from '#lib/components/Debrief.svelte';
	import MissionMenu from '#lib/components/MissionMenu.svelte';
	import Terminal from '#lib/components/Terminal.svelte';

	const session = new GameSession();
	const stings = new StingTracker();

	// Music follows the screen and the account's danger; stings follow trades that close.
	$effect(() => sound.setMood(moodFor(session.screen, session.state, untrack(() => sound.mood))));
	$effect(() => {
		const g = session.state;
		if (!g) return;
		const sting = stings.observe(session.currentRunId, g.events);
		if (sting) sound.sting(sting);
	});

	onMount(() => {
		settings.load();
		pwa.init();
		// Browsers only allow audio after a user gesture.
		const unlock = () => sound.unlock();
		window.addEventListener('pointerdown', unlock);
		window.addEventListener('keydown', unlock);
		const seed = Number(new URLSearchParams(location.search).get('seed'));
		if (Number.isInteger(seed) && seed > 0) session.fixedSeed = seed;
		void session.init();
		return () => {
			window.removeEventListener('pointerdown', unlock);
			window.removeEventListener('keydown', unlock);
			session.dispose();
		};
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
