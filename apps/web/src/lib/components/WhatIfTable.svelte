<script lang="ts">
	// Each leg's and the position's value, P&L and Greeks, exactly as the engine's what-if returned them.
	import type { WhatIfLegView, WhatIfResult } from '#lib/engine/types.ts';
	import { fmtMoney, fmtNum, fmtPct, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { result, perUnit = true }: { result: WhatIfResult; perUnit?: boolean } = $props();
	const L = $derived(i18n.locale);

	function label(l: WhatIfLegView): string {
		const side = l.side === 'buy' ? '+' : '−';
		if (l.kind === 'perp') return `${side}${fmtNum(Number(l.qty), L, 0)} ${t('lab.perp')}`;
		return `${side}${fmtNum(Number(l.qty), L, 0)} ${l.kind === 'call' ? 'C' : 'P'} ${fmtPrice(l.strike, L)}`;
	}
	const greek = (v: string, d = 4) => fmtNum(Number(v), L, d);
</script>

<table class="whatif" data-testid="whatif-table">
	<caption class="muted small">{t(perUnit ? 'lab.tableCaption' : 'lab.tableCaptionNet')}</caption>
	<thead>
		<tr>
			<th scope="col">{t('lab.leg')}</th>
			<th scope="col">{t('lab.value')}</th>
			<th scope="col">{t('opt.iv')}</th>
			<th scope="col" title={t('lab.deltaHint')}>Δ</th>
			<th scope="col" title={t('lab.gammaHint')}>Γ</th>
			<th scope="col" title={t('lab.thetaHint')}>Θ</th>
			<th scope="col" title={t('lab.vegaHint')}>ν</th>
			<th scope="col">{t('lab.pnl')}</th>
		</tr>
	</thead>
	<tbody>
		{#each result.legs as l, i (i)}
			<tr data-testid="whatif-leg">
				<th scope="row">{label(l)}</th>
				<td class="num" data-testid="whatif-value">{fmtPrice(l.value, L)}</td>
				<td class="num">{#if l.iv != null}{fmtPct(Number(l.iv), L, 1)}{#if l.ivFloored}<span class="attention" title={t('lab.ivFloored')}> *</span>{/if}{:else}—{/if}</td>
				<td class="num" data-testid="whatif-delta">{greek(l.delta)}</td>
				<td class="num">{greek(l.gamma)}</td>
				<td class="num">{greek(l.theta)}</td>
				<td class="num">{greek(l.vega)}</td>
				<td class="num" class:up={Number(l.pnl) > 0} class:down={Number(l.pnl) < 0}>{fmtMoney(l.pnl, L, true)}</td>
			</tr>
		{/each}
	</tbody>
	<tfoot>
		<tr data-testid="whatif-net">
			<th scope="row">{t('lab.net')}</th>
			<td class="num">{fmtMoney(result.net.value, L)}</td>
			<td></td>
			<td class="num">{greek(result.net.delta, 3)}</td>
			<td class="num">{greek(result.net.gamma, 4)}</td>
			<td class="num">{fmtMoney(result.net.theta, L)}</td>
			<td class="num">{fmtMoney(result.net.vega, L)}</td>
			<td class="num" class:up={Number(result.net.pnl) > 0} class:down={Number(result.net.pnl) < 0} data-testid="whatif-net-pnl">{fmtMoney(result.net.pnl, L, true)}</td>
		</tr>
	</tfoot>
</table>
{#if result.legs.some((l) => l.ivFloored)}<p class="muted small">* {t('lab.ivFloored')}</p>{/if}

<style>
	.whatif {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.78rem;
	}
	th,
	td {
		padding: 0.15rem 0.3rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.num {
		text-align: right;
		font-variant-numeric: tabular-nums;
	}
	tfoot th,
	tfoot td {
		border-top: 1px solid var(--muted);
		font-weight: bold;
	}
	caption {
		text-align: left;
	}
	.small {
		font-size: 0.75rem;
		margin: 0;
	}
</style>
