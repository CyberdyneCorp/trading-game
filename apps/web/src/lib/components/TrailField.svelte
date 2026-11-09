<script lang="ts">
	// Trailing-stop distance for an open position: a fixed price distance (ATR presets) or a percent of
	// the best close (policy v16), which keeps the same relative room as the price moves.
	import type { OrderSideName } from '#lib/engine/types.ts';
	import { fmtPrice, parseDecimal } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { trailTriggerFromPercent } from '#lib/game/stops.ts';
	import LevelField, { type LevelChip } from './LevelField.svelte';

	let {
		distance = $bindable(''),
		pct = $bindable(''),
		mode = $bindable('price'),
		tick,
		chips,
		exit,
		close,
		testid = 'protect-trail'
	}: {
		distance?: string;
		pct?: string;
		mode?: 'price' | 'pct';
		tick: number;
		chips: LevelChip[];
		exit: OrderSideName;
		close: number;
		/** Test-id prefix: the protective section's by default, the new-order ticket's planned trail otherwise. */
		testid?: string;
	} = $props();

	const id = $props.id();
	const pctChips = [1, 2, 3, 5];
	const trigger = $derived(pct ? trailTriggerFromPercent(exit, close, parseDecimal(pct), tick) : null);

	function setPct(n: number) {
		pct = i18n.locale === 'pt-BR' ? String(n).replace('.', ',') : String(n);
	}
</script>

<div class="trail" data-testid={`${testid}-field`}>
	<div class="modes" role="radiogroup" aria-label={t('ticket.trailMode')}>
		<button type="button" role="radio" aria-checked={mode === 'price'} class:active={mode === 'price'} onclick={() => (mode = 'price')}>
			{t('ticket.trailByPrice')}
		</button>
		<button type="button" role="radio" aria-checked={mode === 'pct'} class:active={mode === 'pct'} onclick={() => (mode = 'pct')} data-testid={testid === 'protect-trail' ? 'trail-mode-pct' : `${testid}-mode-pct`}>
			{t('ticket.trailByPct')}
		</button>
	</div>
	{#if mode === 'price'}
		<LevelField {testid} label={t('ticket.trail')} bind:value={distance} {tick} {chips} />
	{:else}
		<label for={id}>{t('ticket.trailPct')}</label>
		<input {id} data-testid={`${testid}-pct`} inputmode="decimal" bind:value={pct} placeholder="—" />
		<div class="chips">
			{#each pctChips as n (n)}
				<button type="button" class="chip" onclick={() => setPct(n)}>{n}%</button>
			{/each}
		</div>
		<p class="hint">
			{trigger != null ? t('ticket.trailPctHint', { price: fmtPrice(String(trigger), i18n.locale) }) : t('ticket.trailPctNote')}
		</p>
	{/if}
</div>

<style>
	.trail {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
		min-width: 0;
	}
	.modes {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.25rem;
	}
	.modes button {
		font-size: 0.75rem;
		min-height: 0;
		padding: 0.1rem 0.4rem;
	}
	.modes button:not(.active) {
		opacity: 0.55;
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
	}
	input {
		min-width: 0;
	}
	.chips {
		display: flex;
		flex-wrap: wrap;
		gap: 0.2rem;
	}
	.chip {
		font-size: 0.72rem;
		padding: 0.05rem 0.4rem;
		min-height: 0;
		border-color: var(--line);
	}
	.hint {
		font-size: 0.72rem;
		margin: 0;
		color: var(--muted);
	}
</style>
