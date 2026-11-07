<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtPct, fmtQty, num } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { ORDER_TYPE_LABEL, childTag, orderPriceText, sideLabel } from '#lib/game/eventNames.ts';

	let { session }: { session: GameSession } = $props();
	const orders = $derived(session.state?.orders ?? []);
</script>

<!-- A scrolling panel must take keyboard focus so its rows can be scrolled into view without a mouse (axe scrollable-region-focusable). -->
<!-- svelte-ignore a11y_no_noninteractive_tabindex -->
<section class="panel orders" tabindex="0" aria-labelledby="orders-title">
	<h2 class="panel-title" id="orders-title">{t('orders.title')}</h2>
	{#if orders.length === 0}
		<p class="muted">{t('orders.none')}</p>
	{:else}
		<table>
			<tbody>
				{#each orders as o (o.id)}
					<tr>
						<td class="muted">#{o.id}</td>
						<td>{o.series ? `${session.names.seriesName(o.series, i18n.locale)} ` : ''}{ORDER_TYPE_LABEL[o.type]}</td>
						<td class={o.side === 'buy' ? 'up' : 'down'}>{o.side === 'buy' ? '▲' : '▼'} {sideLabel(o.side)}</td>
						<td class="num">{fmtQty(o.qty, i18n.locale)}</td>
						<td class="num">{orderPriceText(o, i18n.locale)}</td>
						<td>
							{#if o.reduceOnly}<span class="tag info" title="reduce-only">{t('orders.reduceOnly')}</span>{/if}
							{#if num(o.trailRate ?? '0') > 0}
								<span class="tag info" title={t('orders.trailPct')} data-testid="trail-pct">{fmtPct(num(o.trailRate ?? '0'), i18n.locale)}</span>
							{/if}
							{#if o.parentId}<span class="tag muted" title={t('orders.child', { id: o.parentId })} data-testid="order-child">{childTag(o, orders)}</span>{/if}
							{#if o.bracketStop !== '0.00000000' || o.bracketTp !== '0.00000000'}<span class="tag attention">BRK</span>{/if}
						</td>
						<td>
							<button class="mini" onclick={() => session.cancel(o.id)} disabled={session.busy}>{t('orders.cancel')}</button>
						</td>
					</tr>
				{/each}
			</tbody>
		</table>
	{/if}
</section>

<style>
	.orders {
		overflow: auto;
	}
	table {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
	}
	td {
		padding: 0.15rem 0.3rem;
		border-bottom: 1px solid var(--line);
	}
	.mini {
		padding: 0.1rem 0.4rem;
		min-height: 24px;
		font-size: 0.75rem;
	}
</style>
