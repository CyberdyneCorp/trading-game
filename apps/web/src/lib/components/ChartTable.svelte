<script lang="ts">
	// Accessible equivalent of the chart (spec terminal-ui "Accessible chart"): a text summary and a
	// table of every closed bar, paged so a long market renders 50 rows at a time. Hidden from sight
	// (not from screen readers) until opened or focused; the last page ends with the current bar (UX01).
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { barPage, chartSummary, type Ohlcv } from './chartA11y.ts';
	import { candleCells } from './chartScale.ts';

	let { bars, open = false, onClose }: { bars: readonly Ohlcv[]; open?: boolean; onClose?: () => void } = $props();

	/** null follows the latest bar; a number keeps an earlier page while the market advances. */
	let page = $state<number | null>(null);
	const view = $derived(barPage(bars.length, page));
	const rows = $derived(bars.slice(view.start, view.end).map((b, j) => ({ i: view.start + j, b })));

	// Closing the table on screen goes back to following the latest bar.
	$effect(() => {
		if (!open) page = null;
	});

	function go(p: number) {
		page = p >= view.pages - 1 ? null : Math.max(0, p);
	}
</script>

<section class="bar-table" class:open data-testid="chart-table" aria-label={t('chart.table')}>
	<p id="chart-summary" class="summary" data-testid="chart-summary">{chartSummary(bars, i18n.locale)}</p>
	{#if bars.length}
		<div class="pager">
			<button onclick={() => go(view.page - 1)} disabled={view.page === 0} data-testid="table-prev">◀ {t('chart.prevPage')}</button>
			<span role="status" data-testid="table-range">{t('chart.tablePage', { from: view.start, to: view.end - 1, total: bars.length })}</span>
			<button onclick={() => go(view.page + 1)} disabled={view.page === view.pages - 1}>{t('chart.nextPage')} ▶</button>
			<button onclick={() => (page = null)} disabled={page == null}>{t('chart.lastPage')} ⇥</button>
			{#if open && onClose}<button onclick={onClose}>✕</button>{/if}
		</div>
		<div class="scroll">
			<table>
				<caption>{t('chart.table')}</caption>
				<thead><tr><th scope="col">#</th><th scope="col">O</th><th scope="col">H</th><th scope="col">L</th><th scope="col">C</th><th scope="col">V</th></tr></thead>
				<tbody>
					{#each rows as row (row.i)}
						<tr>
							<th scope="row">{row.i}</th>{#each candleCells(row.b, i18n.locale) as cell, j (j)}<td class="num">{cell}</td>{/each}
						</tr>
					{/each}
				</tbody>
			</table>
		</div>
	{/if}
</section>

<style>
	/* Visually hidden (screen readers still read it) until opened or until keyboard focus lands inside. */
	.bar-table:not(.open):not(:focus-within) {
		position: absolute;
		width: 1px;
		height: 1px;
		padding: 0;
		margin: -1px;
		overflow: hidden;
		clip: rect(0, 0, 0, 0);
		white-space: nowrap;
		border: 0;
	}
	.bar-table {
		position: absolute;
		inset: 2.2rem 0.5rem auto auto;
		z-index: 5;
		width: min(30rem, 92%);
		max-height: 80%;
		display: flex;
		flex-direction: column;
		gap: 0.35rem;
		background: var(--panel);
		border: 1px solid var(--line);
		padding: 0.5rem;
		font-size: 0.8rem;
	}
	.summary {
		margin: 0;
	}
	.pager {
		display: flex;
		flex-wrap: wrap;
		gap: 0.3rem;
		align-items: center;
	}
	.pager button {
		font-size: 0.75rem;
		padding: 0 0.4rem;
		min-height: 0;
	}
	.scroll {
		overflow: auto;
		min-height: 0;
	}
	table {
		width: 100%;
		border-collapse: collapse;
	}
	caption {
		text-align: left;
		color: var(--muted);
	}
	th,
	td {
		padding: 0 0.4rem;
		text-align: right;
	}
</style>
