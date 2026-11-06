<script lang="ts">
	// The Academy's options lab (options-whatif spec "Lab in the Academy"): an illustrative market at 100,
	// legs the player edits, and the engine's valuation under price, time and IV shifts. No run is needed.
	import { onMount } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import type { AcademyLeg } from '#lib/game/whatifLab.ts';
	import WhatIfLab from './WhatIfLab.svelte';

	let { session }: { session: GameSession } = $props();

	// A call at the money for 30 days at 60% IV; its premium is set from the engine on mount.
	let legs = $state<AcademyLeg[]>([{ kind: 'call', side: 'buy', strike: 100, qty: 1, days: 30, ivPct: 60, entry: 0 }]);
	let ready = $state(false);

	onMount(async () => {
		const r = await session.whatIf({
			forward: '100',
			minutesPerStep: 1440,
			multiplier: '1',
			legs: [{ kind: 'call', side: 'buy', qty: '1', strike: '100', minutesToExpiry: 30 * 1440, iv: '0.6', entry: '0' }]
		});
		if (r.ok) legs = [{ ...legs[0], entry: Number(Number(r.value.legs[0].value).toFixed(2)) }];
		ready = true;
	});
</script>

<main class="labscreen panel" aria-labelledby="lab-title">
	<header>
		<h1 id="lab-title">{t('lab.title')}</h1>
		<button onclick={() => session.backToMenu()}>◀ {t('top.menu')}</button>
	</header>
	<p class="muted">{t('lab.intro')}</p>
	<p class="tag attention">{t('lab.illustrative')}</p>
	{#if ready}
		<WhatIfLab {session} mode="academy" bind:academyLegs={legs} />
	{/if}
</main>

<style>
	.labscreen {
		max-width: 960px;
		margin: 1rem auto;
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
	}
	h1 {
		margin: 0;
		font-family: var(--font-pixel);
	}
</style>
