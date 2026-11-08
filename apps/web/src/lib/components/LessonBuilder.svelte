<script lang="ts">
	// The strategy builder inside a lesson step (design D2 `builder`): the templates on a synthetic chain
	// of one expiry, each leg priced at the engine's model value (tl_option_whatif, rounded to the cent),
	// then the engine's analysis of the structure. Analysis only: nothing is sent and no run is read.
	import type { BuilderWidget } from '#lib/content/academy.ts';
	import type { WhatIfResult } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtMoney, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { TEMPLATES, strategyOf, templateLegs, templateShortable, type TemplateId } from '#lib/game/strategies.ts';
	import { academyRequest, builderAcademyLegs, pricedLegs, unboundedAbove, ZERO_SHIFT } from '#lib/game/whatifLab.ts';
	import PayoffChart from './PayoffChart.svelte';
	import Term from './Term.svelte';
	import WhatIfTable from './WhatIfTable.svelte';

	let { session, widget }: { session: GameSession; widget: BuilderWidget } = $props();
	const L = $derived(i18n.locale);

	let template = $state<TemplateId | null>(null);
	let short = $state(false);
	let result = $state<WhatIfResult | null>(null);
	let error = $state<string | null>(null);
	let ticket = 0;

	$effect(() => {
		if (template === null && widget.template) template = widget.template;
	});

	// Model prices first (entries at 0), then the analysis with those prices as the entries.
	$effect(() => {
		const id = template;
		const mine = ++ticket;
		const legs = id ? builderAcademyLegs(templateLegs(id, widget.strikes, widget.forward, 1, short), widget) : null;
		if (!legs) {
			result = null;
			return;
		}
		void session.whatIf(academyRequest(legs, ZERO_SHIFT, widget.forward)).then(async (priced) => {
			if (mine !== ticket) return;
			if (!priced.ok) return void (error = priced.error);
			const r = await session.whatIf(academyRequest(pricedLegs(legs, priced.value), ZERO_SHIFT, widget.forward));
			if (mine !== ticket) return;
			result = r.ok ? r.value : null;
			error = r.ok ? null : r.error;
		});
	});
</script>

<figure class="widget" data-testid="lesson-builder">
	<figcaption>
		<span>{t('academy.market', { market: widget.market, interval: t('academy.builder.caption', { days: widget.days }) })}</span>
		<span class="tag attention">{t('academy.illustrative')}</span>
	</figcaption>
	<div class="templates" role="group" aria-label={t('builder.templates')}>
		{#each TEMPLATES as id (id)}
			<button class:primary={template === id} aria-pressed={template === id} onclick={() => (template = id)} data-testid={`lesson-template-${id}`}>
				{t(`strategy.${strategyOf(id, short)}`)}
			</button>
		{/each}
	</div>
	<label class="check"><input type="checkbox" bind:checked={short} disabled={!template || !templateShortable(template)} /> {t('builder.short')}</label>
	<p class="muted small">{t('academy.builder.note', { iv: widget.ivPct })}</p>
	{#if error}
		<p class="warnbox" role="alert">{t(error === 'unsupported' ? 'lab.unsupported' : 'lab.error')}</p>
	{:else if result?.expiry}
		{@const e = result.expiry}
		{@const above = unboundedAbove(result)}
		<dl class="kv" aria-live="polite" data-testid="lesson-builder-analysis">
			<dt><Term id="breakeven">{t('opt.breakeven')}</Term></dt><dd>{e.breakevens.map((b) => fmtPrice(b, L)).join(' · ') || '—'}</dd>
			<dt>{t('lab.maxProfit')}</dt><dd>{e.maxProfit != null ? fmtMoney(e.maxProfit, L) : t('lab.unbounded')}</dd>
			<dt><Term id="defined-risk">{t('lab.maxLoss')}</Term></dt><dd data-testid="lesson-builder-max-loss">{e.maxLoss != null ? fmtMoney(e.maxLoss, L) : t('lab.unboundedAbove', { price: above ? fmtPrice(above, L) : '—' })}</dd>
		</dl>
		<PayoffChart {result} testid="lesson-builder-payoff" />
		<WhatIfTable {result} step="day" />
	{:else}
		<p class="muted small">{t('academy.builder.pick')}</p>
	{/if}
</figure>

<style>
	.widget {
		margin: 0.6rem 0;
		border: 1px solid var(--line);
		padding: 0.5rem;
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	figcaption {
		display: flex;
		justify-content: space-between;
		font-size: 0.8rem;
		color: var(--muted);
	}
	.templates {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(10rem, 1fr));
		gap: 0.25rem;
	}
	.templates button {
		font-size: 0.78rem;
	}
	.check {
		font-size: 0.8rem;
		display: flex;
		gap: 0.3rem;
		align-items: center;
	}
	.small {
		font-size: 0.75rem;
		margin: 0;
	}
	.warnbox {
		border: 1px solid var(--danger);
		color: var(--danger);
		padding: 0.3rem 0.5rem;
		font-size: 0.8rem;
		margin: 0;
	}
</style>
