<script lang="ts">
	// Structured plan before an exposure-increasing order (spec gameplay-loop "Plan before advance").
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import type { OrderDraft } from './OrderTicket.svelte';
	import { fmtMoney, parseDecimal } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let {
		draft,
		step,
		onConfirm,
		onCancel,
		optionLabel = null
	}: {
		draft: OrderDraft;
		step: number;
		onConfirm: (p: PlanEntry) => void;
		onCancel: () => void;
		optionLabel?: string | null;
	} = $props();
	let loseScenario = $state('');

	let thesis = $state('');
	let invalidation = $state('');
	let target = $state('');
	let confidence = $state(60);

	$effect(() => {
		invalidation = draft.stop ?? '';
		target = draft.takeProfit ?? '';
	});

	// A typed level in either locale ("95,56" or "95.56"); empty or unreadable means none.
	function level(v: string): number | null {
		const n = parseDecimal(v);
		return Number.isFinite(n) && n > 0 ? n : null;
	}

	function finish(skipped: boolean) {
		onConfirm({
			step,
			side: draft.side,
			thesis: skipped ? '' : thesis.trim(),
			invalidation: skipped ? null : level(invalidation),
			target: skipped ? null : level(target),
			confidence: skipped ? 0 : confidence,
			plannedRisk: draft.estimate.plannedRisk,
			skipped,
			loseScenario: skipped || !optionLabel ? undefined : loseScenario.trim(),
			instrument: optionLabel ?? undefined
		});
	}

	function onKey(e: KeyboardEvent) {
		if (e.key === 'Escape') onCancel();
	}
</script>

<svelte:window onkeydown={onKey} />

<div class="modal-backdrop" role="presentation">
	<div class="modal" role="dialog" aria-modal="true" aria-labelledby="plan-title">
		<h2 id="plan-title">{t('plan.title')}</h2>
		<p class="muted">{t('plan.why')}</p>
		<p class={draft.side === 'buy' ? 'up' : 'down'}>
			{optionLabel ? `${draft.side === 'buy' ? '▲' : '▼'} ${optionLabel} × ${draft.qty}` : `${draft.side === 'buy' ? '▲ LONG' : '▼ SHORT'} ${draft.qty} · ${draft.leverage}x`} ·
			{t('rail.plannedRisk')}: {draft.estimate.plannedRisk != null ? fmtMoney(draft.estimate.plannedRisk, i18n.locale) : '—'}
		</p>
		<label>
			{t('plan.thesis')}
			<!-- svelte-ignore a11y_autofocus -->
			<textarea rows="3" bind:value={thesis} autofocus></textarea>
		</label>
		<div class="two">
			<label>{t('plan.invalidation')}<input inputmode="decimal" bind:value={invalidation} /></label>
			<label>{t('plan.target')}<input inputmode="decimal" bind:value={target} /></label>
		</div>
		{#if optionLabel}
			<label>{t('opt.loseScenario')}<textarea rows="2" bind:value={loseScenario}></textarea></label>
		{/if}
		<label>
			{t('plan.confidence')}: <strong>{confidence}%</strong>
			<input type="range" min="50" max="95" step="5" bind:value={confidence} />
		</label>
		<div class="actions">
			<button onclick={onCancel}>{t('plan.cancel')}</button>
			<button onclick={() => finish(true)}>{t('plan.skip')}</button>
			<button class="primary" onclick={() => finish(false)} disabled={thesis.trim().length < 3}>{t('plan.save')}</button>
		</div>
	</div>
</div>

<style>
	label {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
		margin: 0.5rem 0;
		color: var(--muted);
		font-size: 0.85rem;
	}
	.two {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.6rem;
	}
	.actions {
		display: flex;
		gap: 0.5rem;
		justify-content: flex-end;
		flex-wrap: wrap;
		margin-top: 0.8rem;
	}
</style>
