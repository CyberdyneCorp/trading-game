<script lang="ts">
	// Working orders: type, side, size, price, time in force and expiry bar (policy v18), bracket and
	// OCO marks, cancel; and the player-built OCO link (design D7): tick two orders, "Link OCO", confirm.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtPct, fmtQty, num } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { ORDER_TYPE_LABEL, childTag, orderPriceText, sideLabel } from '#lib/game/eventNames.ts';
	import { linkMark, linkPartner, linkProblem, tifCell } from '#lib/game/orderRules.ts';
	import { rejectionText } from '#lib/game/rejections.ts';
	import ConfirmDialog from './ConfirmDialog.svelte';
	import Term from './Term.svelte';

	let { session }: { session: GameSession } = $props();
	const orders = $derived(session.state?.orders ?? []);
	const v18 = $derived(session.orderRulesV18);

	// Selection for a link: at most two working orders; ids that stopped working drop out.
	let picked = $state<number[]>([]);
	$effect(() => {
		const live = new Set(orders.map((o) => o.id));
		if (picked.some((id) => !live.has(id))) picked = picked.filter((id) => live.has(id));
	});
	const pair = $derived(picked.length === 2 ? picked.map((id) => orders.find((o) => o.id === id)!) : null);
	const problem = $derived(pair ? linkProblem(pair[0], pair[1]) : null);
	let confirming = $state(false);

	function toggle(id: number) {
		picked = picked.includes(id) ? picked.filter((x) => x !== id) : [...picked, id].slice(-2);
	}

	async function link() {
		confirming = false;
		if (!pair) return;
		const [a, b] = pair;
		const res = await session.linkOco(a.id, b.id);
		if (!res) return;
		if (!res.accepted) {
			session.notice = { tone: 'error', text: `✖ ${rejectionText(res.reason)}` };
			return;
		}
		picked = [];
		session.notice = { tone: 'ok', text: t('orders.linkedOk', { a: a.id, b: b.id }) };
	}

	const tifText = (kind: string) => (kind === 'bars' ? t('tif.bars') : kind.toUpperCase());
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
					{@const tif = tifCell(o)}
					{@const mark = linkMark(o, orders)}
					<tr data-testid="order-row" data-order={o.id}>
						<td class="muted">#{o.id}</td>
						{#if v18}
							<td>
								<input
									type="checkbox"
									checked={picked.includes(o.id)}
									onchange={() => toggle(o.id)}
									aria-label={t('orders.select', { id: o.id })}
									data-testid={`order-pick-${o.id}`}
								/>
							</td>
						{/if}
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
							{#if v18 && o.type !== 'market'}
								<span class="tag muted" title={t('orders.tifTitle', { kind: t(`tif.${tif.kind}`) })} data-testid={`order-tif-${o.id}`}>
									{tifText(tif.kind)}{tif.until != null ? ` · ${t('orders.until', { step: tif.until })}` : ''}
								</span>
							{/if}
							{#if mark}
								<span class="tag attention" title={t('orders.linked', { id: linkPartner(o, orders)?.id ?? '' })} data-testid={`order-link-${o.id}`}>{mark}</span>
							{/if}
						</td>
						<td>
							<button class="mini" onclick={() => session.cancel(o.id)} disabled={session.busy}>{t('orders.cancel')}</button>
						</td>
					</tr>
				{/each}
			</tbody>
		</table>
		{#if v18 && orders.length >= 2}
			<div class="link-row">
				<button class="mini" disabled={!pair || problem != null || session.busy} onclick={() => (confirming = true)} data-testid="orders-link">⛓ {t('orders.link')}</button>
				<span class="muted small"><Term id="oco">OCO</Term>: {problem ? t(`orders.linkProblem.${problem}`) : t('orders.linkHint')}</span>
			</div>
		{/if}
	{/if}
</section>

{#if confirming && pair}
	<ConfirmDialog
		title={t('orders.linkTitle', { a: pair[0].id, b: pair[1].id })}
		message={t('orders.linkBody')}
		confirmLabel={t('orders.linkYes')}
		cancelLabel={t('orders.linkNo')}
		onConfirm={() => void link()}
		onCancel={() => (confirming = false)}
	/>
{/if}

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
	.link-row {
		display: flex;
		align-items: center;
		gap: 0.4rem;
		flex-wrap: wrap;
		margin-top: 0.3rem;
	}
	.small {
		font-size: 0.75rem;
	}
</style>
