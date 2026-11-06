<script lang="ts">
	// A small lesson data table (design D2 `table`): numbers in the locale, text cells from the step.
	import type { StepText, TableWidget } from '#lib/content/academy.ts';
	import { fmtNum } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { widget, text }: { widget: TableWidget; text: StepText } = $props();
	const L = $derived(i18n.locale);

	function cell(v: number | string, col: TableWidget['columns'][number]): string {
		if (typeof v === 'string') return text.cells?.[v] ?? v;
		const digits = col.digits ?? (Number.isInteger(v) ? 0 : 2);
		return `${fmtNum(v, L, digits)}${col.suffix ?? ''}`;
	}
</script>

<figure class="lesson-table" data-testid="lesson-table">
	<figcaption>
		<span>{t('academy.market', { market: widget.market, interval: t('academy.data') })}</span>
		<span class="tag attention">{t('academy.illustrative')}</span>
	</figcaption>
	<table>
		<thead>
			<tr>{#each widget.columns as c, k (c.id)}<th scope="col">{text.columns?.[k] ?? c.id}</th>{/each}</tr>
		</thead>
		<tbody>
			{#each widget.rows as row, r (r)}
				<tr>{#each row as v, k (k)}<td class:num={typeof v === 'number'}>{cell(v, widget.columns[k])}</td>{/each}</tr>
			{/each}
		</tbody>
	</table>
</figure>

<style>
	.lesson-table {
		margin: 0.6rem 0;
		border: 1px solid var(--line);
		padding: 0.4rem;
		background: var(--bg);
		overflow-x: auto;
	}
	figcaption {
		display: flex;
		justify-content: space-between;
		font-size: 0.75rem;
		color: var(--muted);
	}
	table {
		border-collapse: collapse;
		width: 100%;
		font-size: 0.85rem;
	}
	th,
	td {
		border-bottom: 1px solid var(--line);
		padding: 0.2rem 0.4rem;
		text-align: left;
	}
	td.num {
		text-align: right;
		font-family: var(--font-mono);
	}
</style>
