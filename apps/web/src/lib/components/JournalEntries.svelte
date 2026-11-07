<script lang="ts">
	// The plan entries of one run (journal screen and the in-game journal).
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import { fmtMoney, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { entries, comboNote = () => null }: { entries: readonly PlanEntry[]; comboNote?: (id: string) => string | null } = $props();
	const L = $derived(i18n.locale);

	// Old entries may hold NaN (a decimal comma read before the fix): shown as no level.
	const level = (v: number | null) => (v != null && Number.isFinite(v) && v > 0 ? fmtPrice(String(v), L) : null);

	function details(p: PlanEntry): string[] {
		const out = [t('journal.conf', { v: p.confidence })];
		const inv = level(p.invalidation);
		const target = level(p.target);
		if (inv) out.push(t('journal.inv', { v: inv }));
		if (target) out.push(t('journal.target', { v: target }));
		if (p.plannedRisk != null && p.plannedRisk > 0) out.push(t('journal.risk', { v: fmtMoney(p.plannedRisk, L) }));
		return out;
	}
</script>

<ol class="entries">
	{#each entries as p, i (i)}
		<li data-testid="journal-entry">
			<span class="muted">[{t('journal.bar', { step: p.step })}]</span>
			<span class={p.side === 'buy' ? 'up' : 'down'}>{p.side === 'buy' ? '▲ LONG' : '▼ SHORT'}{p.instrument ? ` · ${p.instrument}` : ''}</span>
			{#if p.combo && comboNote(p.combo)}<span class="small" data-testid="journal-combo">{comboNote(p.combo)}</span>{/if}
			{#if p.skipped}
				<span class="muted">— {t('journal.skipped')}</span>
			{:else}
				<span class="thesis">{p.thesis}</span>
				<span class="muted small">{details(p).join(' · ')}</span>
				{#if p.loseScenario}<span class="muted small">{t('journal.lose', { v: p.loseScenario })}</span>{/if}
			{/if}
		</li>
	{/each}
</ol>

<style>
	.entries {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 0.35rem;
	}
	.entries li {
		display: flex;
		flex-wrap: wrap;
		gap: 0.2rem 0.6rem;
		align-items: baseline;
	}
	.thesis {
		font-style: italic;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
