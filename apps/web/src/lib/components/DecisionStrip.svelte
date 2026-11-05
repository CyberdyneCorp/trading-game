<script lang="ts">
	// The engine's decision records (task 4.2): one cell per advanced decision step, acted, held or waited.
	import type { DecisionView } from '#lib/engine/types.ts';
	import { decisionCounts } from '#lib/game/debrief.ts';
	import { t } from '#lib/game/i18n.svelte.ts';

	let { decisions }: { decisions: DecisionView[] } = $props();
	const counts = $derived(decisionCounts(decisions));
</script>

<section class="panel" data-testid="debrief-decisions">
	<h2 class="panel-title">{t('debrief.decisions')}</h2>
	<p data-testid="decisions-summary">{t('debrief.decisionsSummary', { ...counts, n: decisions.length })}</p>
	<div class="strip" role="list">
		{#each decisions as d (d.step)}
			<span class="cell {d.kind}" role="listitem" title={`[${d.step}] ${t(`decision.${d.kind}`)}`} data-kind={d.kind}></span>
		{/each}
	</div>
	<p class="legend small">
		<span class="cell act"></span> {t('decision.act')} <span class="cell hold"></span> {t('decision.hold')}
		<span class="cell wait"></span> {t('decision.wait')}
	</p>
	<p class="muted small">{t('debrief.decisionsNote')}</p>
</section>

<style>
	.strip {
		display: flex;
		flex-wrap: wrap;
		gap: 1px;
		margin: 0.4rem 0;
	}
	.cell {
		display: inline-block;
		width: 6px;
		height: 14px;
		background: var(--muted, #555);
	}
	.cell.act {
		background: var(--info);
	}
	.cell.hold {
		background: var(--attention);
	}
	.cell.wait {
		background: var(--border, #333);
		outline: 1px solid var(--muted, #666);
	}
	.legend .cell {
		vertical-align: middle;
		margin-left: 0.5rem;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
