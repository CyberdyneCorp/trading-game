<script lang="ts">
	import type { GameState } from '#lib/engine/types.ts';
	import { fmtMoney, fmtPct, fmtPrice, fmtQty, num, signMark } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { positionGreeks, seriesLabel } from '#lib/game/optionsMath.ts';

	let {
		game,
		plannedRisk,
		onMargin
	}: { game: GameState; plannedRisk: number | null; onMargin?: (amount: string) => void } = $props();
	let marginAmount = $state('100');
	function margin(sign: 1 | -1) {
		const v = Math.abs(Number(marginAmount));
		if (onMargin && Number.isFinite(v) && v > 0) onMargin((sign * v).toFixed(2));
	}

	const L = $derived(i18n.locale);
	const a = $derived(game.account);
	const qty = $derived(num(game.position.qty));
	const unreal = $derived(num(a.unrealized));
	const equity = $derived(num(a.equity));
	const pnlTotal = $derived(equity - num(game.initialBalance));
	const liq = $derived(a.liquidationPrice ? num(a.liquidationPrice) : null);
	const liqDist = $derived(liq != null ? Math.abs(num(a.mark) - liq) / num(a.mark) : null);
	const liqDanger = $derived(liqDist != null && liqDist < 0.03);
	const funding = $derived(num(a.nextFundingRate));
	const fundingIn = $derived(a.nextFundingStep - game.step);
	const levEff = $derived(equity > 0 ? num(a.notional) / equity : 0);
	const opts = $derived(game.options);
	const greeks = $derived(positionGreeks(opts));
	function quoteOf(series: number) {
		return opts.chain.find((q) => q.id === series);
	}
</script>

<aside class="rail">
	<section class="panel">
		<h2 class="panel-title">{t('rail.equity')}</h2>
		<div class="big num">{fmtMoney(a.equity, L)}</div>
		<div class="num {pnlTotal >= 0 ? 'up' : 'down'}">
			{signMark(pnlTotal)} {fmtMoney(pnlTotal, L, true)} ({fmtPct(pnlTotal / num(game.initialBalance), L, 2, true)})
		</div>
		<dl class="kv">
			<dt>{t('rail.wallet')}</dt><dd>{fmtMoney(a.wallet, L)}</dd>
			<dt>{t('rail.available')}</dt><dd>{fmtMoney(a.available, L)}</dd>
			<dt>{t('rail.realized')}</dt><dd>{fmtMoney(game.totals.realizedPnl, L, true)}</dd>
			<dt>{t('rail.fees')}</dt><dd>{fmtMoney(game.totals.feesPaid, L)}</dd>
			<dt>{t('rail.funding')}</dt><dd>{fmtMoney(game.totals.fundingNet, L, true)}</dd>
		</dl>
	</section>

	<section class="panel">
		<h2 class="panel-title">{t('rail.position')}</h2>
		{#if qty === 0}
			<p class="muted">{t('rail.flat')} · {t('rail.leverage')} {game.leverage}x</p>
		{:else}
			<p class="side {qty > 0 ? 'up' : 'down'}">
				{qty > 0 ? '▲ ' + t('rail.long') : '▼ ' + t('rail.short')} {fmtQty(String(Math.abs(qty)), L)}
			</p>
			<dl class="kv">
				<dt>{t('rail.entry')}</dt><dd>{fmtPrice(game.position.entry, L)}</dd>
				<dt>{t('rail.unrealized')}</dt>
				<dd class={unreal >= 0 ? 'up' : 'down'}>{signMark(unreal)} {fmtMoney(unreal, L, true)}</dd>
				<dt>{t('rail.margin')}</dt><dd>{fmtMoney(game.position.margin, L)}</dd>
				<dt>{t('rail.notional')}</dt><dd>{fmtMoney(a.notional, L)}</dd>
				<dt>{t('rail.leverage')}</dt><dd>{game.leverage}x <span class="muted" title={t('rail.effective')}>({levEff.toFixed(1)}x {t('rail.effective')})</span></dd>
			</dl>
			<div class="liq" class:danger={liqDanger} role={liqDanger ? 'alert' : undefined}>
				<div class="muted">{t('rail.liq')}</div>
				<div class="liq-price num">{liq != null ? fmtPrice(String(liq), L) : '—'}</div>
				<div class="num">{t('rail.liqDistance')}: {liqDist != null ? fmtPct(liqDist, L) : '—'}</div>
				{#if liqDanger}<div class="attention">⚠ {t('rail.liqWarning')}</div>{/if}
			</div>
			{#if onMargin}
				<div class="margin-ctl">
					<input aria-label={t('rail.marginAmount')} inputmode="decimal" bind:value={marginAmount} />
					<button onclick={() => margin(1)}>{t('rail.addMargin')}</button>
					<button onclick={() => margin(-1)}>{t('rail.removeMargin')}</button>
				</div>
			{/if}
		{/if}
		{#if plannedRisk != null}
			<dl class="kv"><dt>{t('rail.plannedRisk')}</dt><dd>{fmtMoney(plannedRisk, L)}</dd></dl>
		{/if}
	</section>

	{#if opts.enabled && opts.positions.length > 0}
		<section class="panel">
			<h2 class="panel-title">{t('opt.positions')}</h2>
			<ul class="opts">
				{#each opts.positions as p (p.series)}
					{@const q = quoteOf(p.series)}
					{@const qty = num(p.qty)}
					{@const markPx = q ? num(qty > 0 ? q.bid : q.ask) : 0}
					{@const pnl = (markPx - num(p.avgPremium)) * qty * num(opts.multiplier)}
					<li>
						<span class={qty > 0 ? 'up' : 'down'}>{qty > 0 ? '▲' : '▼'} {q ? seriesLabel(q) : `#${p.series}`} × {Math.abs(qty)}</span>
						<span class="num {pnl >= 0 ? 'up' : 'down'}">{signMark(pnl)} {fmtMoney(pnl, L, true)}</span>
					</li>
				{/each}
			</ul>
			<dl class="kv">
				<dt>{t('opt.value')}</dt><dd>{fmtMoney(num(opts.longValue) - num(opts.shortValue), L, true)}</dd>
				{#if num(opts.initialMargin) > 0}
					<dt>{t('opt.marginUsed')}</dt><dd>{fmtMoney(opts.initialMargin, L)} / {fmtMoney(opts.maintenanceMargin, L)}</dd>
				{/if}
				<dt>{t('opt.bookGreeks')}</dt>
				<dd class="small">Δ {greeks.delta.toFixed(2)} · Γ {greeks.gamma.toFixed(3)} · Θ {greeks.theta.toFixed(2)} · ν {greeks.vega.toFixed(2)}</dd>
			</dl>
		</section>
	{/if}

	<section class="panel">
		<h2 class="panel-title">Funding</h2>
		<dl class="kv">
			<dt>{t('rail.nextFunding')}</dt>
			<dd class={funding > 0 ? 'down' : funding < 0 ? 'up' : ''}>{fmtPct(funding, L, 4, true)}</dd>
			<dt></dt><dd class="muted">{t('rail.fundingIn', { n: fundingIn })}</dd>
			<dt></dt>
			<dd class="muted">{funding !== 0 ? t('rail.fundingWho', { who: funding > 0 ? t('rail.longs') : t('rail.shorts') }) : ''}</dd>
		</dl>
	</section>

	<section class="panel">
		<h2 class="panel-title">{t('rail.objective')}</h2>
		<ul class="objectives">
			{#each missionText(game.mission, L).objectives as o (o)}
				<li>{o}</li>
			{/each}
		</ul>
	</section>
</aside>

<style>
	.rail {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
		min-width: 0;
		overflow: auto;
	}
	/* Panels keep their height and the rail scrolls; shrinking them clipped the position details. */
	.rail > :global(*) {
		flex-shrink: 0;
	}
	.big {
		font-family: var(--font-pixel);
		font-size: 2rem;
		text-align: left;
	}
	.side {
		font-family: var(--font-pixel);
		font-size: 1.5rem;
		margin: 0 0 0.3rem;
	}
	.liq {
		margin-top: 0.5rem;
		border: 1px dashed var(--danger);
		padding: 0.4rem 0.5rem;
	}
	.liq.danger {
		background: rgba(255, 95, 122, 0.12);
	}
	.liq-price {
		font-family: var(--font-pixel);
		font-size: 1.5rem;
		color: var(--danger);
		text-align: left;
	}
	.margin-ctl {
		display: grid;
		grid-template-columns: 1fr auto auto;
		gap: 0.3rem;
		margin-top: 0.4rem;
	}
	.margin-ctl button {
		padding: 0.2rem 0.5rem;
		font-size: 0.8rem;
	}
	.opts {
		list-style: none;
		margin: 0 0 0.3rem;
		padding: 0;
		font-size: 0.82rem;
	}
	.opts li {
		display: flex;
		justify-content: space-between;
		gap: 0.5rem;
	}
	.small {
		font-size: 0.75rem;
	}
	.objectives {
		margin: 0;
		padding-left: 1.1rem;
		font-size: 0.85rem;
		color: var(--muted);
	}
	p {
		margin: 0.2rem 0;
	}
</style>
