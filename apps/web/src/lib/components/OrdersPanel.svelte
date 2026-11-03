<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { session }: { session: GameSession } = $props();
	const orders = $derived(session.state?.orders ?? []);
	const typeLabel: Record<string, string> = { market: 'MKT', limit: 'LMT', stop_market: 'STOP', take_profit: 'TP' };
</script>

<section class="panel orders">
	<h2 class="panel-title">{t('orders.title')}</h2>
	{#if orders.length === 0}
		<p class="muted">{t('orders.none')}</p>
	{:else}
		<table>
			<tbody>
				{#each orders as o (o.id)}
					<tr>
						<td class="muted">#{o.id}</td>
						<td>{typeLabel[o.type]}</td>
						<td class={o.side === 'buy' ? 'up' : 'down'}>{o.side === 'buy' ? '▲' : '▼'} {o.side.toUpperCase()}</td>
						<td class="num">{fmtQty(o.qty, i18n.locale)}</td>
						<td class="num">{o.type === 'market' ? '@open' : fmtPrice(o.price, i18n.locale)}</td>
						<td>{#if o.reduceOnly}<span class="tag info" title="reduce-only">{t('orders.reduceOnly')}</span>{/if}</td>
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
