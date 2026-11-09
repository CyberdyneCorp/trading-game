<script lang="ts">
	// What-if lab (options-whatif spec): price, time and IV sliders over a position's legs; the payoff at
	// expiry and at the slider's time and IV, values, P&L and Greeks, all from the engine's kernel.
	// Academy mode: editable legs on an illustrative market (tl_option_whatif). Terminal mode: the run's
	// series and IV model at today's level (tl_run_option_whatif), which agrees with the chain at zero.
	import type { WhatIfLegInput, WhatIfResult } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { centsSign, fmtMoney, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import {
		ACADEMY_FORWARD,
		IV_RANGE,
		PRICE_RANGE,
		SLIDERS,
		ZERO_SHIFT,
		academyMaxDays,
		academyRequest,
		clampShift,
		isZero,
		runRequest,
		setSlider,
		shiftedForward,
		unboundedAbove,
		analyticsOf,
		type AcademyLeg,
		type LabShift,
		type Slider
	} from '#lib/game/whatifLab.ts';
	import AcademyLegs from './AcademyLegs.svelte';
	import PayoffChart from './PayoffChart.svelte';
	import WhatIfTable from './WhatIfTable.svelte';
	import HeatmapPanel from './HeatmapPanel.svelte';

	let {
		session,
		mode,
		runLegs = [],
		forward = ACADEMY_FORWARD,
		maxBars = 0,
		academyLegs = $bindable<AcademyLeg[]>([]),
		editable = true,
		unlocked = SLIDERS
	}: {
		session: GameSession;
		mode: 'academy' | 'terminal';
		/** Terminal: the legs by series id (or the perpetual). */
		runLegs?: WhatIfLegInput[];
		/** Terminal: the last price; Academy: the illustrative market's price. */
		forward?: number;
		/** Terminal: bars until the nearest expiry. */
		maxBars?: number;
		academyLegs?: AcademyLeg[];
		editable?: boolean;
		/** The sliders a lesson lets the player move (the others stay at zero). */
		unlocked?: readonly Slider[];
	} = $props();

	const L = $derived(i18n.locale);
	let shift = $state<LabShift>({ ...ZERO_SHIFT });
	let result = $state<WhatIfResult | null>(null);
	let error = $state<string | null>(null);
	let ticket = 0;

	const maxTime = $derived(mode === 'terminal' ? maxBars : academyMaxDays(academyLegs));

	$effect(() => {
		const s = clampShift(shift, maxTime);
		const request = mode === 'terminal' ? runRequest(runLegs, s, forward) : academyRequest(academyLegs, s, forward);
		const mine = ++ticket;
		void (mode === 'terminal' ? session.runWhatIf(request) : session.whatIf(request)).then((r) => {
			if (mine !== ticket) return; // a newer slider position superseded this reply
			result = r.ok ? r.value : null;
			error = r.ok ? null : r.error;
		});
	});

	// Sets each leg's entry to the engine's model value now (no shift): the premium a fair trade would pay.
	async function priceAtModel() {
		const unpriced = academyLegs.map((l) => ({ ...l, entry: l.kind === 'perp' ? forward : 0 }));
		const r = await session.whatIf(academyRequest(unpriced, ZERO_SHIFT, forward));
		if (!r.ok) return;
		academyLegs = unpriced.map((l, i) => (l.kind === 'perp' ? l : { ...l, entry: Number(Number(r.value.legs[i].value).toFixed(2)) }));
	}

	function onSlide(which: Slider, e: Event) {
		shift = setSlider(shift, which, Number((e.currentTarget as HTMLInputElement).value), unlocked);
	}

	const sliderMin = (s: Slider) => (s === 'price' ? -PRICE_RANGE : s === 'iv' ? -IV_RANGE : 0);
	const sliderMax = (s: Slider) => (s === 'price' ? PRICE_RANGE : s === 'iv' ? IV_RANGE : maxTime);

	function sliderText(s: Slider): string {
		if (s === 'price') return `${shift.price > 0 ? '+' : ''}${shift.price}% → ${fmtPrice(shiftedForward(forward, shift.price), L)}`;
		if (s === 'iv') return t('lab.ivPoints', { n: `${shift.iv > 0 ? '+' : ''}${shift.iv}` });
		return t(mode === 'terminal' ? 'lab.bars' : 'lab.days', { n: shift.time });
	}
</script>

<section class="lab" aria-label={t('lab.title')} data-testid="whatif-lab">
	<p class="note small">{t('lab.model')}{#if mode === 'terminal'} {t('lab.todayIv')}{/if}</p>
	{#if mode === 'academy' && editable}
		<AcademyLegs bind:legs={academyLegs} />
		<button class="price" onclick={priceAtModel} data-testid="lab-price-model">{t('lab.priceAtModel')}</button>
	{/if}
	<div class="sliders">
		{#each SLIDERS as s (s)}
			{@const locked = !unlocked.includes(s)}
			<label class:locked>
				<span>{t(`lab.slider.${s}`)}{#if locked}{' · '}{t('lab.locked')}{/if}</span>
				<input
					type="range"
					min={sliderMin(s)}
					max={sliderMax(s)}
					step="1"
					value={shift[s]}
					disabled={locked || (s === 'time' && maxTime === 0)}
					oninput={(e) => onSlide(s, e)}
					data-testid={`lab-${s}`}
					aria-valuetext={sliderText(s)}
				/>
				<output>{sliderText(s)}</output>
			</label>
		{/each}
		<button onclick={() => (shift = { ...ZERO_SHIFT })} data-testid="lab-reset">{t('lab.reset')}</button>
	</div>
	{#if error}
		<p class="warnbox" role="alert" data-testid="lab-error">{t(error === 'unsupported' ? 'lab.unsupported' : 'lab.error')}</p>
	{:else if result}
		{@const above = unboundedAbove(result)}
		{@const a = analyticsOf(result)}
		<dl class="kv summary" aria-live="polite">
			<dt>{t(isZero(shift) ? 'lab.positionValue' : 'lab.positionValueScenario')}</dt><dd data-testid="lab-value">{fmtMoney(result.net.value, L)}</dd>
			<dt>{t('lab.pnlNow')}</dt><dd data-testid="lab-pnl" class:up={centsSign(result.net.pnl) > 0} class:down={centsSign(result.net.pnl) < 0}>{fmtMoney(result.net.pnl, L, true)}</dd>
			{#if a}
				<dt>{t('opt.breakeven')}</dt><dd data-testid="lab-breakevens">{a.breakevens.map((b) => fmtPrice(b, L)).join(' · ') || '—'}</dd>
				<dt>{t('lab.maxProfit')}</dt><dd>{a.maxProfit != null ? fmtMoney(a.maxProfit, L) : t('lab.unbounded')}</dd>
				<dt>{t('lab.maxLoss')}</dt><dd data-testid="lab-max-loss">{a.maxLoss != null ? fmtMoney(a.maxLoss, L) : t('lab.unboundedAbove', { price: above ? fmtPrice(above, L) : '—' })}</dd>
			{/if}
		</dl>
		{#if result.horizon}
			<!-- Legs on two expiries (add-practice-and-advanced-strategies D4): the engine's horizon. -->
			<p class="muted small" data-testid="lab-horizon">{t('builder.horizon', { step: (session.state?.step ?? 0) + result.horizon.expiryStep })}</p>
		{/if}
		{#if result.atExpiry}<p class="muted small">{t('lab.atExpiryNote')}</p>{/if}
		<PayoffChart {result} />
		<WhatIfTable {result} step={mode === 'terminal' ? 'bar' : 'day'} />
		{#if mode === 'terminal' && session.orderRulesV18}<HeatmapPanel {session} legs={runLegs} testid="lab-heatmap" />{/if}
	{/if}
</section>

<style>
	.lab {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}
	.price {
		align-self: flex-start;
	}
	.note {
		color: var(--attention);
		margin: 0;
	}
	.sliders {
		display: grid;
		gap: 0.3rem;
	}
	.sliders label {
		display: grid;
		grid-template-columns: 9rem 1fr 11rem;
		align-items: center;
		gap: 0.5rem;
		font-size: 0.8rem;
	}
	.sliders label.locked {
		opacity: 0.6;
	}
	output {
		font-variant-numeric: tabular-nums;
	}
	.summary {
		font-size: 0.85rem;
	}
	.small {
		font-size: 0.75rem;
	}
	.warnbox {
		border: 1px solid var(--danger);
		color: var(--danger);
		padding: 0.3rem 0.5rem;
		font-size: 0.8rem;
		margin: 0;
	}
	@media (max-width: 700px) {
		.sliders label {
			grid-template-columns: 1fr;
		}
	}
</style>
