<script lang="ts">
	// Debrief only (scoring v14, report-only; spec gameplay-loop "Debrief views of biases and episodes"):
	// each perpetual position from flat to flat with its entries, partial exits, stop moves, trailing
	// stop, excursions and result in R, all engine numbers.
	import type { PositionEpisode } from '#lib/engine/types.ts';
	import { fmtMoney, fmtNum, fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { episodes, onMoment }: { episodes: PositionEpisode[]; onMoment?: (step: number) => void } = $props();
	const L = $derived(i18n.locale);
	const r = (bp: number | null) => (bp == null ? '—' : `${fmtNum(bp / 10_000, L, 2, true)}R`);
	const fills = (xs: PositionEpisode['entries']) => xs.map((f) => `${fmtQty(f.qty, L)} @ ${fmtPrice(f.price, L)} (${f.step})`).join(' · ');
</script>

<section class="panel" data-testid="debrief-episodes">
	<h2 class="panel-title">{t('episodes.title')}</h2>
	<table class="eps">
		<thead>
			<tr>
				<th>#</th><th>{t('trade.side')}</th><th>{t('episodes.entries')}</th><th>{t('episodes.exits')}</th>
				<th>{t('episodes.stops')}</th><th class="num">R</th><th class="num">MFE · MAE</th>
			</tr>
		</thead>
		<tbody>
			{#each episodes as e (e.n + ':' + e.entryStep)}
				<tr data-testid="episode-row">
					<td>
						{#if onMoment}<button class="link" onclick={() => onMoment(e.entryStep)} aria-label={t('episodes.show', { step: e.entryStep })}>{e.n}</button>{:else}{e.n}{/if}
					</td>
					<td class={e.side === 'long' ? 'up' : 'down'}>{e.side === 'long' ? '▲ LONG' : '▼ SHORT'}</td>
					<td class="small">{fills(e.entries)}{#if e.entries.length > 1}<span class="tag info">{t('episodes.scaled')}</span>{/if}</td>
					<td class="small">{e.exits.length ? fills(e.exits) : t('episodes.open')}</td>
					<td class="small">
						{#each e.stopMoves as m, i (i)}<span class="move {m.kind}" data-testid="episode-move">{t(`episodes.move.${m.kind}`)} {fmtPrice(m.price, L)} ({m.step})</span>{/each}
						{#if e.trailing}<span class="tag">{t('episodes.trailing')}</span>{/if}
						{#if !e.stopMoves.length && !e.trailing}—{/if}
					</td>
					<td class="num {Number(e.net) >= 0 ? 'up' : 'down'}">{r(e.rBp)}<br /><span class="small">{fmtMoney(e.net, L, true)}</span></td>
					<td class="num small">{r(e.mfeRBp)} · {r(e.maeRBp)}</td>
				</tr>
			{/each}
		</tbody>
	</table>
	<p class="muted small">{t('episodes.note')}</p>
</section>

<style>
	.eps {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
	}
	.eps th,
	.eps td {
		padding: 0.2rem 0.4rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
		vertical-align: top;
	}
	.num {
		text-align: right;
	}
	.move {
		display: block;
	}
	.move.away {
		color: var(--down);
	}
	.move.to_breakeven {
		color: var(--up);
	}
	.link {
		background: none;
		border: none;
		color: var(--info);
		text-decoration: underline;
		cursor: pointer;
		padding: 0;
		font: inherit;
	}
</style>
