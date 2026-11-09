<script lang="ts">
	// The planned trailing stop's notice (design D10): the engine attaches no trailing stop to an entry,
	// so "Protect with: trailing stop" records the rule with the plan. On the first decision after the
	// entry filled, this offers it with the engine's first trigger; one click fills the protective
	// ticket, a second (Protect) sends it. Nothing is sent from here.
	import type { GameState } from '#lib/engine/types.ts';
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import { fmtPct, fmtPrice, parseDecimal, priceToTick } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import { trailTrigger } from '#lib/game/orderRules.ts';
	import { trailRateFromPercent } from '#lib/game/stops.ts';

	let {
		game,
		journal,
		onFill
	}: { game: GameState; journal: readonly PlanEntry[]; onFill: (mode: 'price' | 'pct', value: string) => void } = $props();

	const L = $derived(i18n.locale);
	let handled = $state<number[]>([]);
	const pos = $derived(Number(game.position.qty));
	// The plan of the position now open: the last perpetual plan, when it planned a trailing stop.
	const plan = $derived(journal.findLast((e) => !e.instrument) ?? null);
	const exit = $derived(pos > 0 ? 'sell' : 'buy');
	const trailing = $derived(game.orders.some((o) => o.series === 0 && o.type === 'trailing_stop' && o.side === exit));
	const show = $derived(
		plan?.trail != null && pos !== 0 && (pos > 0) === (plan.side === 'buy') && plan.step < game.step && !trailing && !handled.includes(plan.step)
	);

	const trigger = $derived.by(() => {
		if (!plan?.trail) return null;
		const close = String(lastPrice(game));
		const v = parseDecimal(plan.trail.value);
		if (plan.trail.mode === 'pct') {
			const rate = trailRateFromPercent(v);
			return rate ? trailTrigger(exit, close, { rate }, game.contract.tickSize) : null;
		}
		const dist = priceToTick(v, Number(game.contract.tickSize));
		return dist ? trailTrigger(exit, close, { distance: dist }, game.contract.tickSize) : null;
	});

	const rule = $derived(
		plan?.trail ? (plan.trail.mode === 'pct' ? fmtPct(parseDecimal(plan.trail.value) / 100, L) : fmtPrice(String(parseDecimal(plan.trail.value)), L)) : ''
	);

	function fill() {
		if (!plan?.trail) return;
		handled = [...handled, plan.step];
		onFill(plan.trail.mode, plan.trail.value);
	}
</script>

{#if show && plan}
	<section class="panel trail-notice" role="status" aria-live="polite" data-testid="trail-notice">
		<p>{t('trail.notice', { rule, price: trigger ? fmtPrice(trigger, L) : '—' })}</p>
		<div class="row">
			<button class="primary" onclick={fill} data-testid="trail-notice-place">{t('trail.place')}</button>
			<button onclick={() => (handled = [...handled, plan.step])} data-testid="trail-notice-dismiss">{t('trail.dismiss')}</button>
		</div>
	</section>
{/if}

<style>
	.trail-notice {
		border-color: var(--attention);
	}
	p {
		margin: 0 0 0.3rem;
		font-size: 0.85rem;
	}
	.row {
		display: flex;
		gap: 0.4rem;
		flex-wrap: wrap;
	}
</style>
