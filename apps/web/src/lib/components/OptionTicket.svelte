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
		/** Time in force (policy v18): limits only; absent = gtc. */
		tif?: import('#lib/engine/types.ts').TimeInForceName;
		tifBars?: number;
	}
</script>

<script lang="ts">
	import type { GameState, OptionQuoteView } from '#lib/engine/types.ts';
	import { fmtMoney, fmtNum, fmtPrice, fmtQty, num, parseDecimal, priceToTick, qtyToLot } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { breakeven, legFromQuote, maxLoss, optionBuyCost, optionFee, payoffCurve, seriesLabel, writeMargin } from '#lib/game/optionsMath.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import { barsLeft, tifOrder, type TifChoice } from '#lib/game/orderRules.ts';
	import TifField from './TifField.svelte';

	let {
		game,
		quote,
		side = $bindable('buy'),
		busy,
		tifAllowed = false,
		onSubmit
	}: {
		game: GameState;
		quote: OptionQuoteView | null;
		side?: 'buy' | 'sell';
		busy: boolean;
		/** The engine build takes a time in force (policy v18). */
		tifAllowed?: boolean;
		onSubmit: (d: OptionDraft) => void;
	} = $props();

	let tifChoice = $state<TifChoice>('gtc');
	let tifBarsInput = $state('3');
	// An option order's last eligible bar is before its settlement window (the engine refuses one inside it).
	const tifLeft = $derived(Math.min(barsLeft(game), quote ? Math.max(0, quote.expiryStep - game.step - 1) : 0));
	const tif = $derived(tifAllowed ? tifOrder(tifChoice, parseDecimal(tifBarsInput), game.stepSeconds, tifLeft) : { tif: 'gtc' as const });

	const L = $derived(i18n.locale);
	const mult = $derived(num(game.options.multiplier));
	const lot = $derived(num(game.options.lotSize));
	const f = $derived(lastPrice(game)); // the underlying options are priced on (not the mark)

	let type = $state<'market' | 'limit'>('market');
	let qtyInput = $state('1');
	let limitInput = $state('');

	const qty = $derived(Math.floor((parseDecimal(qtyInput) || 0) / lot + 1e-9) * lot);
	const held = $derived(num(game.options.positions.find((p) => p.series === quote?.id)?.qty ?? '0'));
	const opensShort = $derived(side === 'sell' && qty > Math.max(0, held));
	const shortBlocked = $derived(opensShort && !game.options.allowShort);
	const leg = $derived(quote ? legFromQuote(quote, side, qty, mult) : null);
	const premium = $derived(type === 'limit' && parseDecimal(limitInput) > 0 ? parseDecimal(limitInput) : leg?.premium ?? 0);
	const legAtPx = $derived(leg ? { ...leg, premium } : null);
	// Signed underlying units of the order, for its Greeks (a write is short the option).
	const units = $derived(qty * mult * (side === 'buy' ? 1 : -1));
	const loss = $derived(legAtPx ? maxLoss(legAtPx) : null);
	const margin = $derived(quote && opensShort ? writeMargin(quote, f, qty, mult) : null);
	const feeRate = $derived(num(game.options.feeRate ?? '0'));
	const fee = $derived(legAtPx ? optionFee(legAtPx, f, feeRate) : 0);
	// A buy pays the premium plus the fee, a write posts margin: either must fit in the available balance
	// (the engine's check on placement).
	const cost = $derived(opensShort ? margin : side === 'buy' && legAtPx ? optionBuyCost(legAtPx, f, feeRate) : null);
	const cantAfford = $derived(cost != null && cost > num(game.account.available));
	// The engine cancels a market sell with no buyer (bid 0,00): refuse it here, with the reason.
	const noBid = $derived(!!quote && side === 'sell' && type === 'market' && num(quote.bid) <= 0);
	// Inside the settlement window the engine refuses the order (series_settling, policy v17).
	const settling = $derived(!!quote?.settling);
	const canSubmit = $derived(
		!!quote && !busy && qty > 0 && !shortBlocked && !cantAfford && !noBid && !settling && !('error' in tif) && (type === 'market' || parseDecimal(limitInput) > 0)
	);

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
			label: seriesLabel(quote, L),
			side,
			type,
			qty: qtyToLot(qty, lot),
			premium: type === 'limit' ? priceToTick(parseDecimal(limitInput), num(game.options.premiumTick)) : null,
			opensExposure: side === 'buy' ? held >= 0 : opensShort,
			risk: loss ?? margin,
			...('error' in tif || tif.tif === 'gtc' ? {} : tif)
		});
	}
</script>

<section class="panel oticket" aria-label={t('opt.ticket')}>
	<h2 class="panel-title">{t('opt.ticket')}</h2>
	{#if !quote}
		<p class="muted">{t('opt.none')}</p>
	{:else}
		<p class="series">{seriesLabel(quote, L)} <span class="tag attention">{t('opt.simulated')}</span></p>
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
		{#if tifAllowed}
			<TifField bind:choice={tifChoice} bind:barsInput={tifBarsInput} {type} stepSeconds={game.stepSeconds} step={game.step} left={tifLeft} testid="opt-tif" />
		{/if}
		<dl class="kv est">
			<dt>{t('opt.premium')}</dt><dd>{fmtPrice(String(premium), L)} × {fmtQty(String(qty), L)} × {mult}</dd>
			<dt>{t('opt.breakeven')}</dt><dd>{legAtPx ? fmtPrice(String(breakeven(legAtPx)), L) : '—'}</dd>
			{#if loss != null}<dt>{t('opt.maxLoss')}</dt><dd class="down">{fmtMoney(loss, L)}</dd>{/if}
			<dt>{t('ticket.estFee')}</dt><dd>{fmtMoney(fee, L)}</dd>
			{#if margin != null}<dt>{t('opt.margin')}</dt><dd>{fmtMoney(margin, L)}</dd>{/if}
			<dt>{t('opt.greeks')}</dt>
			<dd class="greeks">
				Δ {fmtNum(num(quote.delta) * units, L)} ·
				Θ {fmtNum(num(quote.theta) * units, L, 3)} ·
				ν {fmtNum(num(quote.vega) * units, L)}
			</dd>
		</dl>
		{#if opensShort && !shortBlocked}<p class="warnbox">⚠ {t('opt.unbounded')}</p>{/if}
		{#if noBid}
			<p class="warnbox" role="alert" data-testid="opt-no-bid">⚠ {t('opt.noBid')}</p>
		{/if}
		{#if settling}
			<p class="warnbox" role="alert" data-testid="opt-settling">⚠ {t('opt.settling', { min: game.options.settlementMinutes ?? 30 })}</p>
		{/if}
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
	/* The side not chosen is dashed, not faded (as in OrderTicket): a faded Sell failed WCAG AA contrast. */
	.sides button:not(.active) {
		border-style: dashed;
	}
	.sides button.active.buy {
		background: rgba(57, 255, 136, 0.15);
	}
	.sides button.active.sell {
		background: rgba(255, 95, 122, 0.15);
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
