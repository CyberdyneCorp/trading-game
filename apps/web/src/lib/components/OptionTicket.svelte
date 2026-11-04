<script lang="ts" module>
	export interface OptionDraft {
		series: number;
		label: string;
		side: 'buy' | 'sell';
		type: 'market' | 'limit';
		qty: string;
		premium: string | null;
		opensExposure: boolean;
		risk: number | null; // max loss for buys, initial margin for writes (planning reference)
	}
</script>

<script lang="ts">
	import type { GameState, OptionQuoteView } from '#lib/engine/types.ts';
	import { fmtMoney, fmtPrice, num, priceToTick, qtyToLot } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { breakeven, legFromQuote, maxLoss, payoffCurve, seriesLabel, writeMargin } from '#lib/game/optionsMath.ts';

	let {
		game,
		quote,
		side = $bindable('buy'),
		busy,
		onSubmit
	}: {
		game: GameState;
		quote: OptionQuoteView | null;
		side?: 'buy' | 'sell';
		busy: boolean;
		onSubmit: (d: OptionDraft) => void;
	} = $props();

	const L = $derived(i18n.locale);
	const mult = $derived(num(game.options.multiplier));
	const lot = $derived(num(game.options.lotSize));
	const f = $derived(num(game.account.mark));

	let type = $state<'market' | 'limit'>('market');
	let qtyInput = $state('1');
	let limitInput = $state('');

	const qty = $derived(Math.floor((num(qtyInput) || 0) / lot + 1e-9) * lot);
	const held = $derived(num(game.options.positions.find((p) => p.series === quote?.id)?.qty ?? '0'));
	const opensShort = $derived(side === 'sell' && qty > Math.max(0, held));
	const shortBlocked = $derived(opensShort && !game.options.allowShort);
	const leg = $derived(quote ? legFromQuote(quote, side, qty, mult) : null);
	const premium = $derived(type === 'limit' && num(limitInput) > 0 ? num(limitInput) : leg?.premium ?? 0);
	const legAtPx = $derived(leg ? { ...leg, premium } : null);
	const loss = $derived(legAtPx ? maxLoss(legAtPx) : null);
	const margin = $derived(quote && opensShort ? writeMargin(quote, f, qty, mult) : null);
	// A buy pays the premium, a write posts margin: either must fit in the available balance.
	const cost = $derived(opensShort ? margin : side === 'buy' ? loss : null);
	const cantAfford = $derived(cost != null && cost > num(game.account.available));
	const canSubmit = $derived(!!quote && !busy && qty > 0 && !shortBlocked && !cantAfford && (type === 'market' || num(limitInput) > 0));

	// Payoff diagram geometry (SVG viewBox units).
	const W = 300;
	const H = 120;
	const curve = $derived.by(() => {
		if (!legAtPx || qty <= 0) return null;
		const k = legAtPx.strike;
		const lo = Math.min(k, f) * 0.8;
		const hi = Math.max(k, f) * 1.2;
		const pts = payoffCurve(legAtPx, lo, hi);
		const maxAbs = Math.max(1e-9, ...pts.map((p) => Math.abs(p.pnl)));
		const x = (s: number) => ((s - lo) / (hi - lo)) * W;
		const y = (v: number) => H / 2 - (v / maxAbs) * (H / 2 - 6);
		return {
			path: pts.map((p, i) => `${i ? 'L' : 'M'}${x(p.s).toFixed(1)},${y(p.pnl).toFixed(1)}`).join(' '),
			zero: y(0),
			markX: x(f),
			beX: x(breakeven(legAtPx)),
			lo,
			hi
		};
	});

	function submit() {
		if (!quote || !canSubmit) return;
		onSubmit({
			series: quote.id,
			label: seriesLabel(quote),
			side,
			type,
			qty: qtyToLot(qty, lot),
			premium: type === 'limit' ? priceToTick(num(limitInput), num(game.options.premiumTick)) : null,
			opensExposure: side === 'buy' ? held >= 0 : opensShort,
			risk: loss ?? margin
		});
	}
</script>

<section class="panel oticket" aria-label={t('opt.ticket')}>
	<h2 class="panel-title">{t('opt.ticket')}</h2>
	{#if !quote}
		<p class="muted">{t('opt.none')}</p>
	{:else}
		<p class="series">{seriesLabel(quote)} <span class="tag attention">{t('opt.simulated')}</span></p>
		<div class="sides" role="radiogroup">
			<button class="buy" class:active={side === 'buy'} role="radio" aria-checked={side === 'buy'} onclick={() => (side = 'buy')}>▲ {t('opt.buy')}</button>
			<button class="sell" class:active={side === 'sell'} role="radio" aria-checked={side === 'sell'} onclick={() => (side = 'sell')}>▼ {t('opt.sell')}</button>
		</div>
		{#if shortBlocked}<p class="attention small">{t('opt.shortLocked')}</p>{/if}
		<div class="grid">
			<label>{t('opt.qty')}<input inputmode="decimal" bind:value={qtyInput} /></label>
			<label>
				{t('ticket.type')}
				<select bind:value={type}>
					<option value="market">{t('opt.market')}</option>
					<option value="limit">{t('opt.limit')}</option>
				</select>
			</label>
			{#if type === 'limit'}
				<label class="full">{t('opt.limit')}<input inputmode="decimal" bind:value={limitInput} placeholder={fmtPrice(side === 'buy' ? quote.ask : quote.bid, L)} /></label>
			{/if}
		</div>
		<dl class="kv est">
			<dt>{t('opt.premium')}</dt><dd>{fmtPrice(String(premium), L)} × {qty} × {mult}</dd>
			<dt>{t('opt.breakeven')}</dt><dd>{legAtPx ? fmtPrice(String(breakeven(legAtPx)), L) : '—'}</dd>
			{#if loss != null}<dt>{t('opt.maxLoss')}</dt><dd class="down">{fmtMoney(loss, L)}</dd>{/if}
			{#if margin != null}<dt>{t('opt.margin')}</dt><dd>{fmtMoney(margin, L)}</dd>{/if}
			<dt>{t('opt.greeks')}</dt>
			<dd class="greeks">
				Δ {(num(quote.delta) * qty * mult * (side === 'buy' ? 1 : -1)).toFixed(2)} ·
				Θ {(num(quote.theta) * qty * mult * (side === 'buy' ? 1 : -1)).toFixed(3)} ·
				ν {(num(quote.vega) * qty * mult * (side === 'buy' ? 1 : -1)).toFixed(2)}
			</dd>
		</dl>
		{#if opensShort && !shortBlocked}<p class="warnbox">⚠ {t('opt.unbounded')}</p>{/if}
		{#if cantAfford}
			<p class="warnbox" role="alert" data-testid="opt-cant-afford">
				⚠ {t(opensShort ? 'opt.cantAffordMargin' : 'opt.cantAffordPremium', { avail: fmtMoney(game.account.available, L) })}
			</p>
		{/if}
		{#if curve}
			<figure>
				<figcaption class="muted small">{t('opt.payoff')}</figcaption>
				<svg viewBox="0 0 {W} {H}" role="img" aria-label={t('opt.payoff')}>
					<line x1="0" x2={W} y1={curve.zero} y2={curve.zero} class="zero" />
					<line x1={curve.markX} x2={curve.markX} y1="0" y2={H} class="mark" />
					<line x1={curve.beX} x2={curve.beX} y1="0" y2={H} class="be" />
					<path d={curve.path} class="pnl" />
				</svg>
				<div class="axis muted small"><span>{curve.lo.toFixed(0)}</span><span>{curve.hi.toFixed(0)}</span></div>
			</figure>
		{/if}
		<button class="primary submit" onclick={submit} disabled={!canSubmit}>{t('opt.submit')}</button>
	{/if}
</section>

<style>
	.oticket {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
		overflow: auto;
	}
	.series {
		font-family: var(--font-pixel);
		font-size: 1.3rem;
		margin: 0;
		color: var(--info);
	}
	.sides {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.3rem;
	}
	.sides button:not(.active) {
		opacity: 0.55;
	}
	.grid {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.35rem 0.5rem;
	}
	.grid .full {
		grid-column: 1 / -1;
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
		display: flex;
		flex-direction: column;
		gap: 0.15rem;
	}
	.est {
		font-size: 0.82rem;
	}
	.greeks {
		font-size: 0.78rem;
	}
	figure {
		margin: 0;
	}
	svg {
		width: 100%;
		height: 120px;
		background: #081010;
		border: 1px solid var(--line);
	}
	.zero {
		stroke: var(--muted);
		stroke-dasharray: 3 3;
	}
	.mark {
		stroke: var(--info);
	}
	.be {
		stroke: var(--attention);
		stroke-dasharray: 2 2;
	}
	.pnl {
		fill: none;
		stroke: var(--accent);
		stroke-width: 2;
	}
	.axis {
		display: flex;
		justify-content: space-between;
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
	.submit {
		width: 100%;
	}
</style>
