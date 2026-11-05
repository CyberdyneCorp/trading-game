<script lang="ts" module>
	export interface LevelChip {
		label: string;
		title: string;
		value: number | null; // null = unavailable (e.g. ATR still warming up)
	}
</script>

<script lang="ts">
	// A price input for a stop or target with one-click presets, a "pick on chart" button and a
	// distance readout (spec terminal-ui "Stop and target tools").
	import { priceInputText } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let {
		label,
		value = $bindable(''),
		chips,
		tick,
		hint = null,
		warn = null,
		testid,
		onPick
	}: {
		label: string;
		value?: string;
		chips: LevelChip[];
		tick: number;
		hint?: string | null;
		warn?: string | null;
		testid: string;
		onPick?: (set: (price: number) => void) => void;
	} = $props();

	const id = $props.id();

	function set(price: number) {
		value = priceInputText(price, tick, i18n.locale);
	}
</script>

<div class="field" data-testid={testid}>
	<div class="head">
		<label for={id}>{label}</label>
		{#if value}<button class="clear" type="button" onclick={() => (value = '')} aria-label={t('level.clear')}>✕</button>{/if}
	</div>
	<div class="row">
		<input {id} inputmode="decimal" bind:value placeholder="—" aria-invalid={warn ? 'true' : undefined} />
		{#if onPick}
			<button type="button" class="pick" title={t('level.pick')} aria-label={t('level.pick')} onclick={() => onPick(set)}>⌖</button>
		{/if}
	</div>
	<div class="chips">
		{#each chips as c (c.label)}
			<button type="button" class="chip" title={c.title} disabled={c.value == null} onclick={() => c.value != null && set(c.value)}>
				{c.label}
			</button>
		{/each}
	</div>
	{#if warn}<p class="warn">⚠ {warn}</p>{:else if hint}<p class="hint">{hint}</p>{/if}
</div>

<style>
	.field {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
		min-width: 0;
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
	}
	.head {
		display: flex;
		justify-content: space-between;
		align-items: center;
	}
	.row {
		display: flex;
		gap: 0.25rem;
	}
	.row input {
		flex: 1;
		min-width: 0;
	}
	.pick {
		padding: 0 0.5rem;
		min-height: 0;
	}
	.clear {
		background: none;
		border: none;
		color: var(--muted);
		padding: 0;
		min-height: 0;
		font-size: 0.75rem;
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
	.chip:disabled {
		opacity: 0.35;
	}
	.hint,
	.warn {
		font-size: 0.72rem;
		margin: 0;
	}
	.hint {
		color: var(--muted);
	}
	.warn {
		color: var(--attention);
	}
</style>
