<script lang="ts">
	// P&L of a position at expiry (solid) and at the lab's time and IV (dashed), from the engine's curves
	// (tl_option_whatif / tl_run_option_whatif). Breakevens dotted, strikes ticked, the forward marked.
	import type { WhatIfResult } from '#lib/engine/types.ts';
	import { fmtMoney, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { analyticsOf, chartGeometry, strikesOf } from '#lib/game/whatifLab.ts';

	let { result, testid = 'payoff-chart' }: { result: WhatIfResult; testid?: string } = $props();

	const W = 320;
	const H = 140;
	const L = $derived(i18n.locale);
	const g = $derived(chartGeometry(result, W, H, strikesOf(result)));
	// Legs on two expiries: the solid curve is the engine's horizon at the first expiry (design D4).
	const horizon = $derived(analyticsOf(result)?.horizonStep ?? null);
	const summary = $derived.by(() => {
		const e = analyticsOf(result);
		const bes = e?.breakevens.map((b) => fmtPrice(b, L)).join(' · ') || '—';
		const profit = e?.maxProfit != null ? fmtMoney(e.maxProfit, L) : t('lab.unbounded');
		const loss = e?.maxLoss != null ? fmtMoney(e.maxLoss, L) : t('lab.unbounded');
		return t('lab.chartSummary', { bes, profit, loss });
	});
</script>

{#if g}
	<figure class="payoff" data-testid={testid}>
		<!-- Stretched to the box (the legend's scale ends sit at its edges); strokes keep their width. -->
		<svg viewBox="0 0 {W} {H}" preserveAspectRatio="none" role="img" aria-label={summary}>
			<line x1="0" x2={W} y1={g.zeroY} y2={g.zeroY} class="zero" />
			{#each g.strikeX as x, i (i)}<line x1={x} x2={x} y1={H - 6} y2={H} class="strike" />{/each}
			{#each g.breakevenX as x, i (i)}<line x1={x} x2={x} y1="0" y2={H} class="be" />{/each}
			<line x1={g.forwardX} x2={g.forwardX} y1="0" y2={H} class="fwd" />
			{#if g.expiryPath}<path d={g.expiryPath} class="expiry" />{/if}
			<path d={g.todayPath} class="today" />
		</svg>
		<figcaption class="legend small">
			<span>{fmtPrice(String(g.lo), L)}</span>
			<span class="key"><i class="k-expiry"></i>{horizon != null ? t('lab.atFirstExpiry') : t('lab.atExpiry')} <i class="k-today"></i>{t('lab.today')}</span>
			<span>{fmtPrice(String(g.hi), L)}</span>
		</figcaption>
	</figure>
{/if}

<style>
	.payoff {
		margin: 0;
	}
	svg {
		width: 100%;
		height: 140px;
		background: #081010;
		border: 1px solid var(--line);
	}
	svg :global(*) {
		vector-effect: non-scaling-stroke;
	}
	.zero {
		stroke: var(--muted);
		stroke-dasharray: 3 3;
	}
	.fwd {
		stroke: var(--info);
	}
	.be {
		stroke: var(--attention);
		stroke-dasharray: 2 2;
	}
	.strike {
		stroke: var(--muted);
	}
	.expiry {
		fill: none;
		stroke: var(--accent);
		stroke-width: 2;
	}
	.today {
		fill: none;
		stroke: var(--info);
		stroke-width: 1.5;
		stroke-dasharray: 5 3;
	}
	/* The scale ends stay at the box edges; in a narrow panel the key wraps between them instead of
	   running into the axis values. */
	.legend {
		display: flex;
		justify-content: space-between;
		align-items: flex-start;
		gap: 0.6rem;
		color: var(--muted);
	}
	.key {
		flex: 1 1 0;
		min-width: 0;
		text-align: center;
	}
	.key i {
		display: inline-block;
		width: 1rem;
		height: 0;
		vertical-align: middle;
		margin: 0 0.25rem 0 0.5rem;
	}
	.k-expiry {
		border-top: 2px solid var(--accent);
	}
	.k-today {
		border-top: 2px dashed var(--info);
	}
	.small {
		font-size: 0.75rem;
	}
</style>
