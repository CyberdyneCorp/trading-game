<script lang="ts">
	// P&L heatmap (add-practice-and-advanced-strategies D4, W1-W7): the engine's P&L of the legs (or the
	// holdings) at each price (columns) after each number of bars up to the last expiry (rows). A
	// diverging palette that follows the colorblind setting (--up/--down), the focused or hovered cell
	// read on the help line, and a table view for screen readers. Collapsed until opened: it asks the
	// engine for one grid (at most 61 x 25 valuations per leg) only when shown.
	import type { WhatIfHeatmapView, WhatIfLegInput } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import { cellParams, cellStyle, heatmapRequest, heatmapScale } from '#lib/game/heatmap.ts';
	import Term from './Term.svelte';

	let {
		session,
		legs,
		holdings = false,
		testid = 'heatmap'
	}: { session: GameSession; legs: readonly WhatIfLegInput[]; holdings?: boolean; testid?: string } = $props();

	const L = $derived(i18n.locale);
	const game = $derived(session.state!);
	let open = $state(false);
	let table = $state(false);
	let map = $state<WhatIfHeatmapView | null>(null);
	let error = $state<string | null>(null);
	let focus = $state<{ t: number; p: number } | null>(null);
	let ticket = 0;

	// Bars to the last expiry of the legs' (or held) options; the perpetual alone: one day of bars.
	const maxBars = $derived.by(() => {
		const ids = new Set([...legs.map((l) => l.series), ...(holdings ? game.options.positions.map((p) => p.series) : [])]);
		const exp = game.options.chain.filter((q) => ids.has(q.id)).map((q) => q.expiryStep);
		return exp.length ? Math.max(...exp) - game.step : Math.min(24, game.totalBars - 1 - game.step);
	});

	$effect(() => {
		if (!open) return;
		const req = heatmapRequest(legs, lastPrice(game), Number(game.contract.tickSize), maxBars, holdings);
		void game.digest;
		const mine = ++ticket;
		void session.runWhatIf(req).then((r) => {
			if (mine !== ticket) return; // a reply for older legs or an older state
			map = r.ok ? (r.value.heatmap ?? null) : null;
			error = r.ok ? null : r.error;
		});
	});

	const scale = $derived(map ? heatmapScale(map) : 1);
	const readout = $derived(map && focus ? t('heatmap.cell', cellParams(map, focus.t, focus.p, L)) : t('heatmap.caption'));
</script>

<details class="heatmap" bind:open data-testid={testid}>
	<summary>▦ {t('heatmap.open')}</summary>
	<p class="muted small"><Term id="heatmap">{t('heatmap.title')}</Term></p>
	{#if error}
		<p class="muted small">{t(error === 'unsupported' ? 'lab.unsupportedV18' : 'lab.unsupported')}</p>
	{:else if map}
		{@const m = map}
		<p class="readout small" aria-live="polite" data-testid={`${testid}-readout`}>{readout}</p>
		<button class="mini" onclick={() => (table = !table)} data-testid={`${testid}-toggle`}>{table ? t('heatmap.grid') : t('heatmap.table')}</button>
		{#if table}
			<!-- A scrolling region takes keyboard focus (axe scrollable-region-focusable). -->
			<!-- svelte-ignore a11y_no_noninteractive_tabindex -->
			<div class="scroll" tabindex="0" role="region" aria-label={t('heatmap.title')}>
				<table data-testid={`${testid}-table`}>
					<caption class="small">{t('heatmap.caption')}</caption>
					<thead>
						<tr><th scope="col">{t('heatmap.bars')}</th>{#each m.prices as p, i (i)}<th scope="col" class="num">{fmtPrice(p, L)}</th>{/each}</tr>
					</thead>
					<tbody>
						{#each m.pnl as row, ti (ti)}
							<tr><th scope="row">{m.times[ti]}</th>{#each row as v, pi (pi)}<td class="num">{cellParams(m, ti, pi, L).pnl}</td>{/each}</tr>
						{/each}
					</tbody>
				</table>
			</div>
		{:else}
			<div class="grid" role="group" aria-label={t('heatmap.title')} style={`grid-template-columns: 3.2rem repeat(${m.prices.length}, 1fr)`}>
				{#each m.pnl as row, ti (ti)}
					<span class="axis small" aria-hidden="true">{m.times[ti]}</span>
					{#each row as v, pi (pi)}
						<button
							class="cell"
							style={cellStyle(Number(v), scale)}
							aria-label={t('heatmap.cell', cellParams(m, ti, pi, L))}
							onmouseenter={() => (focus = { t: ti, p: pi })}
							onfocus={() => (focus = { t: ti, p: pi })}
							data-testid={`${testid}-cell`}
						></button>
					{/each}
				{/each}
				<span class="axis small" aria-hidden="true">{t('heatmap.bars')}</span>
				{#each m.prices as p, i (i)}<span class="axis small price" aria-hidden="true">{i % 5 === 0 && i < m.prices.length - 1 ? fmtPrice(p, L) : ''}</span>{/each}
			</div>
			<p class="muted small">{t('heatmap.legend')}</p>
		{/if}
	{/if}
</details>

<style>
	.heatmap summary {
		cursor: pointer;
		font-size: 0.82rem;
	}
	.grid {
		display: grid;
		gap: 1px;
		align-items: stretch;
	}
	.cell {
		min-height: 14px;
		min-width: 0;
		padding: 0;
		border: none;
		border-radius: 0;
	}
	.cell:focus-visible,
	.cell:hover {
		outline: 2px solid var(--attention);
		outline-offset: -1px;
	}
	.axis {
		color: var(--muted);
		font-size: 0.68rem;
		white-space: nowrap;
	}
	.axis.price {
		overflow: visible;
	}
	.readout {
		margin: 0.2rem 0;
		min-height: 1.2em;
	}
	.scroll {
		overflow: auto;
		max-height: 16rem;
	}
	table {
		border-collapse: collapse;
		font-size: 0.7rem;
	}
	th,
	td {
		padding: 0.05rem 0.25rem;
		border-bottom: 1px solid var(--line);
	}
	.small {
		font-size: 0.75rem;
	}
	.mini {
		padding: 0.1rem 0.4rem;
		min-height: 24px;
		font-size: 0.75rem;
	}
</style>
