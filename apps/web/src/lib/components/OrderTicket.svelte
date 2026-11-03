<script lang="ts" module>
	import type { OrderSideName, OrderTypeName } from '#lib/engine/types.ts';
	import type { TicketEstimate } from '#lib/game/sizing.ts';

	export interface OrderDraft {
		side: OrderSideName;
		type: OrderTypeName;
		qty: string;
		price: string | null;
		leverage: number;
		stop: string | null;
		takeProfit: string | null;
		estimate: TicketEstimate;
		increases: boolean;
	}
</script>

<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { costsOf, estimate, estimatedMarketEntry, qtyForRisk } from '#lib/game/sizing.ts';
	import { fmtMoney, fmtPrice, num, priceToTick, qtyToLot } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let {
		session,
		side = $bindable('buy'),
		onSubmit
	}: { session: GameSession; side?: OrderSideName; onSubmit: (d: OrderDraft) => void } = $props();

	const s = $derived(session.state!);
	const L = $derived(i18n.locale);
	const k = $derived(costsOf(s.contract));
	const tick = $derived(num(s.contract.tickSize));
	const lot = $derived(num(s.contract.lotSize));
	const mark = $derived(num(s.account.mark));
	const cap = $derived(s.contract.leverageCap);
	const posQty = $derived(num(s.position.qty));

	let type = $state<'market' | 'limit' | 'stop_market'>('market');
	let sizeBy = $state<'qty' | 'risk'>('risk');
	let qtyInput = $state('10');
	let riskPct = $state(1);
	let priceInput = $state('');
	let stopInput = $state('');
	let tpInput = $state('');
	let leverage = $state(1);

	$effect(() => {
		leverage = Math.min(Math.max(1, s.leverage), cap);
	});

	const entry = $derived(type === 'market' ? estimatedMarketEntry(side, mark, k) : num(priceInput) || mark);
	const stop = $derived(stopInput ? num(stopInput) : null);
	const qty = $derived.by(() => {
		if (sizeBy === 'risk' && stop != null) return qtyForRisk(num(s.account.equity), riskPct, entry, stop, k, lot);
		return Math.floor((num(qtyInput) || 0) / lot + 1e-9) * lot;
	});
	const qtyStr = $derived(qtyToLot(qty, lot));
	const est = $derived(estimate({ side, qty, entry, leverage, stop }, k));
	const shortBlocked = $derived(!s.contract.allowShort && side === 'sell' && posQty <= 0);
	const increases = $derived(posQty === 0 || posQty > 0 === (side === 'buy') || qty > Math.abs(posQty));
	const stopWrongSide = $derived(stop != null && (side === 'buy' ? stop >= entry : stop <= entry));
	const needsPrice = $derived(type !== 'market');
	const canSubmit = $derived(
		!session.busy && qty > 0 && !shortBlocked && !stopWrongSide && (!needsPrice || num(priceInput) > 0)
	);

	function submit() {
		if (!canSubmit) return;
		onSubmit({
			side,
			type,
			qty: qtyStr,
			price: needsPrice ? priceToTick(num(priceInput), tick) : null,
			leverage,
			stop: stopInput ? priceToTick(num(stopInput), tick) : null,
			takeProfit: tpInput ? priceToTick(num(tpInput), tick) : null,
			estimate: est,
			increases
		});
	}

	// Suggest a stop 2% away when switching sides (player can edit freely).
	function suggestStop() {
		const d = 0.02 * mark;
		stopInput = priceToTick(side === 'buy' ? mark - d : mark + d, tick);
	}

	// Protection for an existing position.
	let protStop = $state('');
	let protTp = $state('');
	let protTrail = $state('');
	async function protect() {
		const exit: OrderSideName = posQty > 0 ? 'sell' : 'buy';
		const size = s.position.qty.replace('-', '');
		if (protStop) await session.placeOrder('stop_market', exit, size, priceToTick(num(protStop), tick), true);
		if (protTp) await session.placeOrder('take_profit', exit, size, priceToTick(num(protTp), tick), true);
		if (protTrail) await session.placeOrder('trailing_stop', exit, size, null, true, { trail: priceToTick(num(protTrail), tick) });
		protStop = '';
		protTp = '';
		protTrail = '';
	}
</script>

<section class="panel ticket" aria-label={t('ticket.title')}>
	<h2 class="panel-title">{t('ticket.title')}</h2>
	<div class="sides" role="radiogroup">
		<button class="buy" class:active={side === 'buy'} onclick={() => (side = 'buy')} role="radio" aria-checked={side === 'buy'}>
			▲ {t('ticket.buy')}
		</button>
		<button class="sell" class:active={side === 'sell'} onclick={() => (side = 'sell')} role="radio" aria-checked={side === 'sell'}>
			▼ {t('ticket.sell')}
		</button>
	</div>
	{#if shortBlocked}<p class="attention small">{t('ticket.shortLocked')}</p>{/if}

	<div class="grid">
		<label>
			{t('ticket.type')}
			<select bind:value={type}>
				<option value="market">{t('ticket.market')}</option>
				<option value="limit">{t('ticket.limit')}</option>
				<option value="stop_market">{t('ticket.stop')}</option>
			</select>
		</label>
		<label>
			{t('ticket.leverage')} (≤ {cap}x)
			<input type="number" min="1" max={cap} step="1" bind:value={leverage} />
		</label>
		{#if needsPrice}
			<label class="full">
				{type === 'limit' ? t('ticket.price') : t('ticket.trigger')}
				<input inputmode="decimal" bind:value={priceInput} placeholder={fmtPrice(s.account.mark, L)} />
			</label>
		{/if}
		<label>
			{t('ticket.stopLoss')}
			<input inputmode="decimal" bind:value={stopInput} placeholder="—" />
		</label>
		<label>
			{t('ticket.takeProfit')}
			<input inputmode="decimal" bind:value={tpInput} placeholder="—" />
		</label>
		<label>
			{t('ticket.sizeBy')}
			<select bind:value={sizeBy}>
				<option value="risk">{t('ticket.sizeRisk')}</option>
				<option value="qty">{t('ticket.sizeQty')}</option>
			</select>
		</label>
		{#if sizeBy === 'risk'}
			<label>
				{t('ticket.riskPct')}
				<input type="number" min="0.1" max="10" step="0.1" bind:value={riskPct} />
			</label>
		{:else}
			<label>
				{t('ticket.qty')}
				<input inputmode="decimal" bind:value={qtyInput} />
			</label>
		{/if}
	</div>
	{#if sizeBy === 'risk' && stop == null}
		<p class="small muted">↳ {t('ticket.stopLoss')} <button class="link" onclick={suggestStop}>2%?</button></p>
	{/if}

	<dl class="kv est">
		<dt>{t('ticket.qty')}</dt><dd>{qtyStr}</dd>
		<dt>{t('ticket.estEntry')}</dt><dd>{fmtPrice(String(entry), L)}</dd>
		<dt>{t('ticket.estMargin')}</dt><dd>{fmtMoney(est.margin, L)}</dd>
		<dt>{t('ticket.estFee')}</dt><dd>{fmtMoney(est.fee, L)}</dd>
		<dt>{t('ticket.estLiq')}</dt><dd class="down">{est.liquidation ? fmtPrice(String(est.liquidation), L) : '—'}</dd>
		<dt>{t('ticket.estRisk')}</dt><dd>{est.plannedRisk != null ? fmtMoney(est.plannedRisk, L) : '—'}</dd>
	</dl>
	{#if est.stopBeyondLiquidation}<p class="warnbox" role="alert">⚠ {t('ticket.stopBeyondLiq')}</p>{/if}
	{#if stop == null && qty > 0}<p class="small attention">{t('ticket.noStop')}</p>{/if}
	{#if stopWrongSide}<p class="small attention">⚠ stop {side === 'buy' ? '<' : '>'} {fmtPrice(String(entry), L)}</p>{/if}
	<p class="small muted">{t('ticket.attachNote')}</p>

	<button class="primary submit {side}" onclick={submit} disabled={!canSubmit}>
		{side === 'buy' ? '▲' : '▼'} {t('ticket.submit')} <kbd>Enter</kbd>
	</button>

	{#if posQty !== 0}
		<hr />
		<h3>{t('ticket.protect')}</h3>
		<div class="grid">
			<label>{t('ticket.stopLoss')}<input inputmode="decimal" bind:value={protStop} /></label>
			<label>{t('ticket.takeProfit')}<input inputmode="decimal" bind:value={protTp} /></label>
			<label class="full">{t('ticket.trail')}<input inputmode="decimal" bind:value={protTrail} placeholder="2.00" /></label>
		</div>
		<div class="row">
			<button onclick={protect} disabled={session.busy || (!protStop && !protTp && !protTrail)}>{t('ticket.protectSubmit')}</button>
			<button class="warn" onclick={() => session.closePosition()} disabled={session.busy}>✕ {t('ticket.close')} <kbd>X</kbd></button>
		</div>
	{/if}
</section>

<style>
	.ticket {
		display: flex;
		flex-direction: column;
		gap: 0.45rem;
		overflow: auto;
	}
	.sides {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.3rem;
	}
	.sides button.active.buy {
		background: rgba(57, 255, 136, 0.15);
	}
	.sides button.active.sell {
		background: rgba(255, 95, 122, 0.15);
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
		font-size: 0.85rem;
		border-top: 1px dashed var(--line);
		padding-top: 0.35rem;
	}
	.small {
		font-size: 0.78rem;
		margin: 0;
	}
	.warnbox {
		border: 1px solid var(--danger);
		color: var(--danger);
		padding: 0.35rem 0.5rem;
		font-size: 0.82rem;
		margin: 0;
	}
	.submit {
		width: 100%;
		font-size: 1rem;
	}
	.submit.sell {
		border-color: var(--down);
		color: var(--down);
		background: rgba(255, 95, 122, 0.1);
	}
	.row {
		display: flex;
		gap: 0.4rem;
		flex-wrap: wrap;
	}
	.link {
		background: none;
		border: none;
		color: var(--info);
		text-decoration: underline;
		padding: 0;
		min-height: 0;
	}
	hr {
		border: none;
		border-top: 1px solid var(--line);
		width: 100%;
	}
	kbd {
		font-size: 0.7rem;
		opacity: 0.7;
	}
</style>
