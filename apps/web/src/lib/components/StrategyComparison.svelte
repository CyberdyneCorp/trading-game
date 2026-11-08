<script lang="ts">
	// Debrief only (scoring v13, report-only; gameplay-loop "Strategy comparison in the debrief"): the
	// mission's reference structures valued by the engine on this seed, next to the player's own result.
	// Computed after the run, so it uses no future at any decision; never a score input.
	import type { StrategyComparison } from '#lib/engine/types.ts';
	import { comparisonLines } from '#lib/game/debrief.ts';
	import { fmtMoney, fmtQty } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { rejectionText } from '#lib/game/rejections.ts';

	let { comparison, playerNet }: { comparison: StrategyComparison; playerNet: number } = $props();
	const L = $derived(i18n.locale);
	const lines = $derived(comparisonLines(comparison, playerNet));
	const basis = $derived(
		t(comparison.basis === 'player' ? 'comparison.basisPlayer' : 'comparison.basisReference', {
			step: comparison.entryStep,
			qty: fmtQty(comparison.qty, L)
		})
	);
</script>

<section class="panel" data-testid="debrief-comparison">
	<h2 class="panel-title">{t('comparison.title')}</h2>
	<p class="small">{basis}</p>
	<table class="cmp">
		<thead>
			<tr>
				<th>{t('comparison.col.structure')}</th><th class="num">{t('comparison.col.bars')}</th><th class="num">{t('comparison.col.net')}</th>
				<th class="num">{t('comparison.col.drawdown')}</th><th class="num">{t('comparison.col.calls')}</th>
			</tr>
		</thead>
		<tbody>
			{#each lines as l (l.key)}
				<tr class:you={l.key === 'you'} data-testid="comparison-row">
					<th scope="row">{t(l.name)}{#if l.best} <span class="tag info">{t('comparison.best')}</span>{/if}</th>
					{#if l.refused}
						<td colspan="4" class="muted" data-testid="comparison-refused">{t('comparison.refused', { reason: rejectionText(l.refused) })}</td>
					{:else}
						<td class="num">{l.entryStep != null ? `${l.entryStep} → ${l.exitStep}` : '—'}</td>
						<td class="num {l.net != null && l.net > 0 ? 'up' : l.net != null && l.net < 0 ? 'down' : ''}" data-testid="comparison-net">{l.net != null ? fmtMoney(l.net, L, true) : '—'}</td>
						<td class="num">{l.drawdown != null ? fmtMoney(l.drawdown, L) : '—'}</td>
						<td class="num">{l.marginCalls ?? '—'}</td>
					{/if}
				</tr>
			{/each}
		</tbody>
	</table>
	<p class="muted small">{t('comparison.note')}</p>
</section>

<style>
	.cmp {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
	}
	.cmp th,
	.cmp td {
		padding: 0.2rem 0.4rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.cmp .num {
		text-align: right;
		font-variant-numeric: tabular-nums;
	}
	.cmp .tag {
		margin-left: 0.4rem;
	}
	.cmp tr.you th {
		color: var(--attention);
	}
	.small {
		font-size: 0.8rem;
	}
</style>
