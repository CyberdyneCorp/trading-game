<script lang="ts" module>
	import type { EngineEvent } from '#lib/engine/types.ts';
	import type { Locale } from '#lib/game/i18n.svelte.ts';
	import { fmtMoney, fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { reasonText, t } from '#lib/game/i18n.svelte.ts';
	import { EventNames } from '#lib/game/eventNames.ts';

	const NO_NAMES = new EventNames();


	function flags(e: EngineEvent): string {
		const f: string[] = [];
		if (e.gap) f.push(t('feed.gap'));
		if (e.ambiguous) f.push(t('feed.ambiguous'));
		if (e.capacityLimited) f.push(t('feed.capacity'));
		return f.length ? ` [${f.join(', ')}]` : '';
	}

	function fillLine(e: EngineEvent, L: Locale): string {
		// Fills without an order are the engine's forced buy-backs of written options (margin call).
		const vars = { id: e.orderId, side: e.side.toUpperCase(), qty: fmtQty(e.qty, L), price: fmtPrice(e.price, L) };
		const base = t(e.orderId ? 'feed.filled' : 'feed.forcedFill', vars);
		const parts = [base];
		if (Number(e.requested) > 0 && e.requested !== e.price) parts.push(t('feed.requested', { price: fmtPrice(e.requested, L) }));
		parts.push(t('feed.fee', { fee: fmtMoney(e.amount, L) }));
		if (Number(e.pnl) !== 0) parts.push(t('feed.pnl', { pnl: fmtMoney(e.pnl, L, true) }));
		return parts.join(' · ') + flags(e);
	}

	function orderLine(e: EngineEvent, L: Locale, names: EventNames): string | null {
		if (e.status === 'accepted') {
			const type = names.orderType(e.orderId) ?? (e.requested !== '0.00000000' ? 'LMT/STOP/TP' : 'MKT');
			const series = e.series ? `${names.seriesName(e.series)} ` : '';
			return t('feed.accepted', { id: e.orderId, type: series + type, side: e.side, qty: fmtQty(e.qty, L) });
		}
		if (e.status === 'cancelled') return t('feed.cancelled', { id: e.orderId, reason: reasonText(e.reason) });
		if (e.status === 'expired') return t('feed.expired', { id: e.orderId });
		return null;
	}

	export function describeEvent(e: EngineEvent, L: Locale, names: EventNames = NO_NAMES): { text: string; tone: string } | null {
		switch (e.type) {
			case 'fill':
				return { text: (e.series ? `${names.seriesName(e.series)} ` : '') + fillLine(e, L), tone: e.side === 'buy' ? 'info' : 'attention' };
			case 'option_settlement':
				return {
					text: t('feed.settlement', { series: names.seriesName(e.series), price: fmtPrice(e.price, L), value: fmtPrice(e.requested, L), amount: fmtMoney(e.amount, L, true) }) + (e.approximate ? ` [${t('feed.approxSettle')}]` : ''),
					tone: Number(e.amount) >= 0 ? 'up' : 'down'
				};
			case 'margin_adjusted':
				return { text: t('feed.marginAdjusted', { amount: fmtMoney(e.amount, L, true) }), tone: 'info' };
			case 'margin_call':
				return { text: '⚠ ' + t('feed.marginCall', { mm: fmtMoney(e.amount, L) }), tone: 'down' };
			case 'order': {
				const text = orderLine(e, L, names);
				return text ? { text, tone: 'muted' } : null;
			}
			case 'funding':
				return { text: t('feed.funding', { amount: fmtMoney(e.amount, L, true) }), tone: Number(e.amount) >= 0 ? 'up' : 'down' };
			case 'liquidation':
				return { text: '✖ ' + t('feed.liquidation', { price: fmtPrice(e.price, L), amount: fmtMoney(e.amount, L) }) + flags(e), tone: 'down' };
			case 'bankrupt':
				return { text: '☠ ' + t('feed.bankrupt'), tone: 'down' };
			case 'run_finished':
				return { text: t('feed.finished'), tone: 'info' };
			case 'leverage_changed':
				return { text: t('feed.leverage', { lev: Math.round(Number(e.amount) * 1e8) }), tone: 'muted' };
		}
		return null;
	}
</script>

<script lang="ts">
	import { i18n } from '#lib/game/i18n.svelte.ts';

	let { events, names }: { events: EngineEvent[]; names?: EventNames } = $props();
	const lines = $derived(
		events
			.slice()
			.reverse()
			.map((e) => ({ e, d: describeEvent(e, i18n.locale, names) }))
			.filter((x) => x.d != null)
			.slice(0, 80)
	);
</script>

<section class="panel feed">
	<h2 class="panel-title">{t('feed.title')}</h2>
	{#if lines.length === 0}
		<p class="muted">{t('feed.empty')}</p>
	{:else}
		<ol aria-live="polite">
			{#each lines as { e, d } (e.seq)}
				<li class={d?.tone}><span class="muted step">[{e.step}]</span> {d?.text}</li>
			{/each}
		</ol>
	{/if}
</section>

<style>
	.feed {
		overflow: auto;
	}
	ol {
		list-style: none;
		margin: 0;
		padding: 0;
		font-size: 0.8rem;
	}
	li {
		padding: 0.12rem 0;
		border-bottom: 1px solid rgba(36, 65, 58, 0.5);
	}
	.step {
		font-variant-numeric: tabular-nums;
	}
</style>
