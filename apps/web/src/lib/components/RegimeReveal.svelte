<script lang="ts">
	// Debrief only (design D8.1 G4): the regimes this run really went through, as a band under the
	// chart, and how four common reads did inside each of them (engine scoring v9).
	import type { RegimeSignals, RegimeSpan } from '#lib/engine/types.ts';
	import { fmtTally, regimeBand, regimeName, signalRows, SIGNAL_KEYS } from '#lib/game/debrief.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { spans, rows, from, to }: { spans: RegimeSpan[]; rows: RegimeSignals[] | undefined; from: number; to: number } = $props();
	const L = $derived(i18n.locale);
	const band = $derived(regimeBand(spans, from, to));
	const table = $derived(signalRows(rows));
</script>

<section class="panel" data-testid="debrief-regimes">
	<h2 class="panel-title">{t('debrief.regimesTitle')}</h2>
	<div class="band" role="img" aria-label={t('debrief.regimeBand', { from, to })} data-testid="regime-band">
		{#each band as piece (piece.start)}
			<span
				class="piece {piece.label}"
				style:left="{piece.left * 100}%"
				style:width="{piece.width * 100}%"
				title="{regimeName(piece.label, piece.direction)} · {piece.start}–{piece.start + piece.bars - 1}"
				data-label={piece.label}
			>
				{#if piece.width > 0.08}{regimeName(piece.label, piece.direction)}{/if}
			</span>
		{/each}
	</div>
	<p class="muted small">{t('debrief.regimeBand', { from, to })}</p>
	{#if table.length}
		<table class="signals" data-testid="regime-signals">
			<thead>
				<tr>
					<th>{t('regime.col.regime')}</th><th class="num">{t('regime.col.bars')}</th>
					{#each SIGNAL_KEYS as k (k)}<th class="num">{t(`regime.col.${k}`)}</th>{/each}
				</tr>
			</thead>
			<tbody>
				{#each table as row (row.start)}
					<tr>
						<td>{regimeName(row.label, row.direction)} <span class="muted">@{row.start}</span></td>
						<td class="num">{row.bars}</td>
						{#each SIGNAL_KEYS as k (k)}<td class="num">{fmtTally(row[k], L)}</td>{/each}
					</tr>
				{/each}
			</tbody>
		</table>
	{/if}
	<p class="muted small">{t('debrief.regimesNote')}</p>
</section>

<style>
	.band {
		position: relative;
		height: 1.6rem;
		border: 1px solid var(--line);
		background: var(--panel-2);
		overflow: hidden;
	}
	.piece {
		position: absolute;
		top: 0;
		bottom: 0;
		display: flex;
		align-items: center;
		justify-content: center;
		font-size: 0.72rem;
		color: var(--bg);
		white-space: nowrap;
		overflow: hidden;
		border-right: 1px solid var(--bg);
	}
	.range,
	.calm {
		background: var(--info);
	}
	.trend {
		background: var(--up);
	}
	.high_vol,
	.shock {
		background: var(--down);
	}
	.low_liquidity,
	.crowded {
		background: var(--attention);
	}
	.unlabeled {
		background: var(--muted);
	}
	.small {
		font-size: 0.8rem;
	}
	.signals {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
		margin-top: 0.4rem;
	}
	.signals th,
	.signals td {
		padding: 0.2rem 0.4rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.signals .num {
		text-align: right;
	}
</style>
