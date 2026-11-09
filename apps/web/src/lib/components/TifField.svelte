<script lang="ts">
	// Time in force of an order (execution policy v18, design D6): GTC, one day of bars, N bars, IOC and
	// FOK. A choice the order type cannot take is disabled with its reason; the hint says which bars the
	// order can execute on. The engine refuses anything else (invalid_time_in_force).
	import type { OrderTypeName } from '#lib/engine/types.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { fmtNum, parseDecimal } from '#lib/game/format.ts';
	import { dayBars, tifBlock, tifOrder, TIF_CHOICES, type TifChoice, type TifOrder } from '#lib/game/orderRules.ts';
	import Term from './Term.svelte';

	let {
		choice = $bindable('gtc'),
		barsInput = $bindable('3'),
		type,
		stepSeconds,
		step,
		left,
		testid = 'tif'
	}: {
		choice?: TifChoice;
		barsInput?: string;
		type: OrderTypeName;
		stepSeconds: number;
		/** The current decision step: the order is first eligible on the next bar. */
		step: number;
		/** Bars left in the run (the longest bars:N). */
		left: number;
		testid?: string;
	} = $props();

	const id = $props.id();
	const day = $derived(dayBars(stepSeconds));
	const block = $derived(tifBlock(choice, type));
	const order = $derived(tifOrder(choice, parseDecimal(barsInput), stepSeconds, left));
	// A choice the order type cannot take falls back to GTC (the hint below explains the block first).
	$effect(() => {
		if (block) choice = 'gtc';
	});

	function hint(o: TifOrder | { error: string }): string {
		if ('error' in o) return t('tif.barsRange', { left });
		if (o.tif === 'bars') return t('tif.hint.bars', { from: step + 1, to: step + (o.tifBars ?? 1) });
		if (o.tif === 'ioc' || o.tif === 'fok') return t(`tif.hint.${o.tif}`, { from: step + 1 });
		return t('tif.hint.gtc');
	}
</script>

<div class="tif" data-testid={testid}>
	<label for={id}><Term id="time-in-force">{t('tif.label')}</Term></label>
	<select {id} bind:value={choice} data-testid={`${testid}-select`}>
		{#each TIF_CHOICES as c (c)}
			{@const why = tifBlock(c, type)}
			<option value={c} disabled={why != null} title={why ? t(`tif.block.${why}`) : undefined}>
				{c === 'day' ? t('tif.day', { n: fmtNum(Math.min(day, left), i18n.locale, 0) }) : t(`tif.${c}`)}
			</option>
		{/each}
	</select>
	{#if choice === 'bars'}
		<label class="bars">{t('tif.barsInput')}<input inputmode="numeric" bind:value={barsInput} data-testid={`${testid}-bars`} /></label>
	{/if}
	<p class="hint" class:warn={'error' in order} data-testid={`${testid}-hint`}>{hint(order)}</p>
	{#if type !== 'limit'}<p class="hint muted">{t(type === 'market' ? 'tif.block.market_immediate' : 'tif.block.needs_limit')}</p>{/if}
</div>

<style>
	.tif {
		display: flex;
		flex-direction: column;
		gap: 0.15rem;
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
	}
	.bars {
		display: flex;
		align-items: center;
		gap: 0.3rem;
	}
	.bars input {
		width: 4rem;
	}
	.hint {
		font-size: 0.72rem;
		margin: 0;
	}
	.hint.warn {
		color: var(--danger);
	}
</style>
