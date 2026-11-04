<script lang="ts" module>
	import { untrack } from 'svelte';
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

	/** Draft levels the chart previews while the player edits the ticket. */
	export interface TicketPreview {
		entry: number;
		stop: number | null;
		takeProfit: number | null;
		liquidation: number | null;
	}
</script>

<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { costsOf, estimate, estimatedMarketEntry, qtyForRisk, releveredLiquidation } from '#lib/game/sizing.ts';
	import { fmtMoney, fmtPrice, num, priceToTick, qtyToLot } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import {
		distanceOf,
		rewardRisk,
		stopFromAtr,
		stopFromPct,
		stopOnLosingSide,
		swingStop,
		targetFromR,
		targetOnWinningSide,
		type BarTuple
	} from '#lib/game/stops.ts';
	import LevelField, { type LevelChip } from './LevelField.svelte';

	let {
		session,
		side = $bindable('buy'),
		preview = $bindable(null),
		stopInput = $bindable(''),
		tpInput = $bindable(''),
		sizeBy = $bindable('risk'),
		qtyInput = $bindable('10'),
		onSubmit,
		onPick
	}: {
		session: GameSession;
		side?: OrderSideName;
		preview?: TicketPreview | null;
		/** Stop and target fields; bindable so dragging their lines on the chart edits them. */
		stopInput?: string;
		tpInput?: string;
		/** Sizing mode and quantity; bindable so the advisor's suggested size can be applied. */
		sizeBy?: 'qty' | 'risk';
		qtyInput?: string;
		onSubmit: (d: OrderDraft) => void;
		onPick?: (set: (price: number) => void) => void;
	} = $props();

	const s = $derived(session.state!);
	const L = $derived(i18n.locale);
	const k = $derived(costsOf(s.contract));
	const tick = $derived(num(s.contract.tickSize));
	const lot = $derived(num(s.contract.lotSize));
	const mark = $derived(num(s.account.mark));
	const cap = $derived(s.contract.leverageCap);
	const posQty = $derived(num(s.position.qty));
	const atr = $derived.by(() => {
		const v = s.indicators.atr14[s.indicators.atr14.length - 1];
		return v != null ? Number(v) : null;
	});
	const bars = $derived(s.bars.map((b) => b.slice(0, 5).map(Number) as BarTuple));

	let type = $state<'market' | 'limit' | 'stop_market'>('market');
	let riskPct = $state(1);
	let priceInput = $state('');
	let leverage = $state(1);

	$effect(() => {
		leverage = Math.min(Math.max(1, s.leverage), cap);
	});

	// Stop and target belong to the order they were set for: a fill attaches them as bracket orders
	// (or ends the trade), so they are cleared rather than reused to size the next order.
	let fillsSeen = untrack(() => s.totals.fills);
	$effect(() => {
		if (s.totals.fills === fillsSeen) return;
		fillsSeen = s.totals.fills;
		stopInput = '';
		tpInput = '';
	});

	const entry = $derived(type === 'market' ? estimatedMarketEntry(side, mark, k) : num(priceInput) || mark);
	const stop = $derived(stopInput ? num(stopInput) : null);
	const qty = $derived.by(() => {
		if (sizeBy === 'risk' && stop != null) return qtyForRisk(num(s.account.equity), riskPct, entry, stop, k, lot);
		return Math.floor((num(qtyInput) || 0) / lot + 1e-9) * lot;
	});
	const qtyStr = $derived(qtyToLot(qty, lot));
	// The field can hold anything typed; orders and estimates use the value clamped to the mission cap.
	const lev = $derived(Math.min(Math.max(1, Math.round(leverage || 1)), cap));
	const est = $derived(estimate({ side, qty, entry, leverage: lev, stop }, k));
	const shortBlocked = $derived(!s.contract.allowShort && side === 'sell' && posQty <= 0);
	const increases = $derived(posQty === 0 || posQty > 0 === (side === 'buy') || qty > Math.abs(posQty));
	const tp = $derived(tpInput ? num(tpInput) : null);
	const stopWrongSide = $derived(stop != null && !stopOnLosingSide(side, entry, stop));
	const tpWrongSide = $derived(tp != null && !targetOnWinningSide(side, entry, tp));
	const needsPrice = $derived(type !== 'market');
	const canSubmit = $derived(
		!session.busy && qty > 0 && !shortBlocked && !stopWrongSide && !tpWrongSide && (!needsPrice || num(priceInput) > 0)
	);

	function submit() {
		if (!canSubmit) return;
		onSubmit({
			side,
			type,
			qty: qtyStr,
			price: needsPrice ? priceToTick(num(priceInput), tick) : null,
			leverage: lev,
			stop: stopInput ? priceToTick(num(stopInput), tick) : null,
			takeProfit: tpInput ? priceToTick(num(tpInput), tick) : null,
			estimate: est,
			increases
		});
	}

	$effect(() => {
		preview = qty > 0 ? { entry, stop, takeProfit: tp, liquidation: est.liquidation } : null;
	});

	function stopChips(sd: OrderSideName, from: number): LevelChip[] {
		return [
			{ label: '1×ATR', title: t('level.atrTitle', { n: 1 }), value: stopFromAtr(sd, from, atr, 1) },
			{ label: '2×ATR', title: t('level.atrTitle', { n: 2 }), value: stopFromAtr(sd, from, atr, 2) },
			{ label: 'Swing', title: t('level.swingTitle'), value: swingStop(sd, from, bars, tick) },
			{ label: '1%', title: t('level.pctTitle', { n: 1 }), value: stopFromPct(sd, from, 1) },
			{ label: '2%', title: t('level.pctTitle', { n: 2 }), value: stopFromPct(sd, from, 2) }
		];
	}

	function targetChips(sd: OrderSideName, from: number, stopAt: number | null): LevelChip[] {
		return [1, 2, 3].map((r) => ({ label: `${r}R`, title: t('level.rTitle', { n: r }), value: targetFromR(sd, from, stopAt, r) }));
	}

	function stopHint(from: number, level: number | null): string | null {
		const d = distanceOf(from, level, atr);
		if (!d) return null;
		const atrPart = d.atr != null ? ` · ${d.atr.toFixed(1)}×ATR` : '';
		return `${fmtPrice(String(d.abs), L)} · ${d.pct.toFixed(2)}%${atrPart}`;
	}

	function targetHint(sd: OrderSideName, from: number, stopAt: number | null, level: number | null): string | null {
		const base = stopHint(from, level);
		const rr = rewardRisk(sd, from, stopAt, level);
		return base && rr != null ? `${base} · R:R 1:${rr.toFixed(1)}` : base;
	}

	const stopWarn = $derived(stopWrongSide ? t('level.stopSide', { side: side === 'buy' ? '<' : '>', price: fmtPrice(String(entry), L) }) : null);
	const tpWarn = $derived(tpWrongSide ? t('level.tpSide', { side: side === 'buy' ? '>' : '<', price: fmtPrice(String(entry), L) }) : null);

	// Protection for an existing position.
	let protStop = $state('');
	let protTp = $state('');
	let protTrail = $state('');
	const posSide = $derived<OrderSideName>(posQty > 0 ? 'buy' : 'sell');
	const posEntry = $derived(num(s.position.entry));
	const protStopN = $derived(protStop ? num(protStop) : null);
	const protTpN = $derived(protTp ? num(protTp) : null);
	const protStopWarn = $derived(
		protStopN != null && !stopOnLosingSide(posSide, mark, protStopN) ? t('level.protStopSide', { price: fmtPrice(s.account.mark, L) }) : null
	);
	const protTpWarn = $derived(
		protTpN != null && !targetOnWinningSide(posSide, mark, protTpN) ? t('level.protTpSide', { price: fmtPrice(s.account.mark, L) }) : null
	);
	// Sending at another leverage re-margins the open position too (engine set_leverage).
	const relever = $derived.by(() => {
		if (posQty === 0 || lev === s.leverage) return null;
		const to = releveredLiquidation(posSide, posQty, posEntry, lev, k);
		const from = s.account.liquidationPrice ? num(s.account.liquidationPrice) : null;
		return { from, to, closer: to != null && (from == null || Math.abs(mark - to) < Math.abs(mark - from)) };
	});
	const hasStop = $derived(s.orders.some((o) => o.reduceOnly && (o.type === 'stop_market' || o.type === 'trailing_stop')));
	const trailChips = $derived<LevelChip[]>([1, 2, 3].map((m) => ({ label: `${m}×ATR`, title: t('level.trailTitle', { n: m }), value: atr != null ? m * atr : null })));
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
			<input type="number" min="1" max={cap} step="1" bind:value={leverage} onchange={() => (leverage = lev)} />
		</label>
		{#if needsPrice}
			<label class="full">
				{type === 'limit' ? t('ticket.price') : t('ticket.trigger')}
				<input inputmode="decimal" bind:value={priceInput} placeholder={fmtPrice(s.account.mark, L)} />
			</label>
		{/if}
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
	<div class="levels">
		<LevelField
			testid="ticket-stop"
			label={t('ticket.stopLoss')}
			bind:value={stopInput}
			{tick}
			chips={stopChips(side, entry)}
			hint={stopHint(entry, stop)}
			warn={stopWarn}
			{onPick}
		/>
		<LevelField
			testid="ticket-tp"
			label={t('ticket.takeProfit')}
			bind:value={tpInput}
			{tick}
			chips={targetChips(side, entry, stop)}
			hint={targetHint(side, entry, stop, tp)}
			warn={tpWarn}
			{onPick}
		/>
	</div>
	{#if sizeBy === 'risk' && stop == null}
		<p class="small attention">↳ {t('ticket.riskNeedsStop')}</p>
	{/if}

	<dl class="kv est">
		<dt>{t('ticket.qty')}</dt><dd>{qtyStr}</dd>
		<dt>{t('ticket.estEntry')}</dt><dd>{fmtPrice(String(entry), L)}</dd>
		<dt>{t('ticket.estMargin')}</dt><dd>{fmtMoney(est.margin, L)}</dd>
		<dt>{t('ticket.estFee')}</dt><dd>{fmtMoney(est.fee, L)}</dd>
		<dt>{t('ticket.estLiq')}</dt><dd class="down">{est.liquidation ? fmtPrice(String(est.liquidation), L) : '—'}</dd>
		<dt>{t('ticket.estRisk')}</dt><dd>{est.plannedRisk != null ? fmtMoney(est.plannedRisk, L) : '—'}</dd>
	</dl>
	{#if relever}
		<p class={relever.closer ? 'warnbox' : 'small info'} role="status" data-testid="relever-note">
			{relever.closer ? '⚠ ' : ''}{t('ticket.relever', {
				from: s.leverage,
				to: lev,
				liqFrom: relever.from != null ? fmtPrice(String(relever.from), L) : '—',
				liqTo: relever.to != null ? fmtPrice(String(relever.to), L) : '—'
			})}
		</p>
	{/if}
	{#if est.stopBeyondLiquidation}<p class="warnbox" role="alert">⚠ {t('ticket.stopBeyondLiq')}</p>{/if}
	{#if stop == null && qty > 0}<p class="small attention">{t('ticket.noStop')}</p>{/if}
	<p class="small muted">{t('ticket.attachNote')}</p>

	<button class="primary submit {side}" onclick={submit} disabled={!canSubmit}>
		{side === 'buy' ? '▲' : '▼'} {t('ticket.submit')} <kbd>Enter</kbd>
	</button>

	{#if posQty !== 0}
		<hr />
		<h3>{t('ticket.protect')}</h3>
		{#if !hasStop}<p class="warnbox" role="status">⚠ {t('ticket.unprotected')}</p>{/if}
		<div class="levels">
			<LevelField
				testid="protect-stop"
				label={t('ticket.stopLoss')}
				bind:value={protStop}
				{tick}
				chips={stopChips(posSide, mark)}
				hint={stopHint(posEntry, protStopN)}
				warn={protStopWarn}
				{onPick}
			/>
			<LevelField
				testid="protect-tp"
				label={t('ticket.takeProfit')}
				bind:value={protTp}
				{tick}
				chips={targetChips(posSide, posEntry, protStopN)}
				hint={targetHint(posSide, posEntry, protStopN, protTpN)}
				warn={protTpWarn}
				{onPick}
			/>
			<div class="full">
				<LevelField testid="protect-trail" label={t('ticket.trail')} bind:value={protTrail} {tick} chips={trailChips} />
			</div>
		</div>
		<div class="row">
			<button onclick={protect} disabled={session.busy || !!protStopWarn || !!protTpWarn || (!protStop && !protTp && !protTrail)}>
				{t('ticket.protectSubmit')}
			</button>
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
	.grid .full,
	.levels .full {
		grid-column: 1 / -1;
	}
	.levels {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.45rem 0.5rem;
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
