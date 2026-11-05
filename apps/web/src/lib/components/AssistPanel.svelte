<script lang="ts">
	// Assisted practice (task 4.8): rewind to an earlier decision or play the mission on another market.
	// Both start a new attempt flagged assisted; the original attempt is kept as it is.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { rewindRange } from '#lib/game/assisted.ts';
	import { t } from '#lib/game/i18n.svelte.ts';

	let { session, open = false }: { session: GameSession; open?: boolean } = $props();
	const s = $derived(session.state!);
	// Bars are shown as turns from the first decision (1 = the first decision step), like the top bar.
	const range = $derived(rewindRange(s.warmupBars, s.step));
	let turn = $state(1);
	const target = $derived(range ? Math.min(Math.max(s.warmupBars + Math.round(turn) - 1, range.min), range.max) : 0);
</script>

<details class="panel assist" {open} data-testid="assist-panel">
	<summary>{t('assist.title')}</summary>
	{#if session.assistAllowed}
		<p class="small muted">{t('assist.note')}</p>
		<div class="row">
			{#if range}
				<label>
					{t('assist.rewindTo')}
					<input type="number" min="1" max={range.max - s.warmupBars + 1} bind:value={turn} data-testid="assist-turn" />
				</label>
				<button onclick={() => session.rewind(target)} disabled={session.busy} data-testid="assist-rewind">{t('assist.rewind')}</button>
			{/if}
			{#if !s.knownScenario}
				<button onclick={() => session.practiceNewSeed()} disabled={session.busy} data-testid="assist-new-seed">{t('assist.newSeed')}</button>
			{/if}
		</div>
	{:else}
		<p class="small muted">{t('assist.campaignBlocked')}</p>
	{/if}
</details>

<style>
	.assist {
		padding: 0.4rem 0.6rem;
	}
	summary {
		cursor: pointer;
	}
	.row {
		display: flex;
		gap: 0.4rem;
		align-items: center;
		flex-wrap: wrap;
	}
	input {
		width: 5rem;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
