<script lang="ts" module>
	import { untrack } from 'svelte';
	import type { OrderSideName, OrderTypeName, TimeInForceName } from '#lib/engine/types.ts';
	import type { TicketEstimate } from '#lib/game/sizing.ts';
	import type { PlannedTrail } from '#lib/game/orderRules.ts';

	export type TicketOrderType = 'market' | 'limit' | 'stop_market';

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
		/** Margin plus fee of the part that adds exposure (the ticket's estimate, quoted if refused). */
		required: number;
		/** Time in force (policy v18); absent = gtc. */
		tif?: TimeInForceName;
		tifBars?: number;
		/** "Protect with: trailing stop" (design D10): recorded with the plan, offered after the fill. */
		plannedTrail?: PlannedTrail;
	}

	/** The protective section's tabs (design D10): the trailing stop has its own, never hidden. */
	export type ProtectTab = 'stop' | 'trail' | 'target';

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
	import Term from './Term.svelte';
	import { costsOf, estimate, estimatedMarketEntry, marginTier, opposingEffect, releveredLiquidation, requiredMargin, shockFactors, sizedQty, type SizeBy } from '#lib/game/sizing.ts';
	import { fmtMoney, fmtNum, fmtPct, fmtPrice, fmtQty, num, parseAmount, parseDecimal, priceToTick, qtyToLot } from '#lib/game/format.ts';
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
		trailRateFromPercent,
		type BarTuple
	} from '#lib/game/stops.ts';
	import LevelField, { type LevelChip } from './LevelField.svelte';
	import { keymap } from '#lib/game/keymap.svelte.ts';
	import { keyLabel } from '#lib/game/shortcuts.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import TrailField from './TrailField.svelte';
	import TifField from './TifField.svelte';
	import { barsLeft, tifOrder, trailTrigger, type TifChoice } from '#lib/game/orderRules.ts';

	let {
		session,
		side = $bindable('buy'),
		preview = $bindable(null),
		stopInput = $bindable(''),
		tpInput = $bindable(''),
		sizeBy = $bindable('risk'),
		qtyInput = $bindable('10'),
		leverage = $bindable(1),
		orderType = $bindable('market'),
		protectTab = $bindable('stop'),
		protTrail = $bindable(''),
		protTrailPct = $bindable(''),
		trailMode = $bindable('price'),
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
		sizeBy?: SizeBy;
		qtyInput?: string;
		/** Leverage field; bindable so applying the advisor's size can raise it to fit the margin. */
		leverage?: number;
		/** Order type; bindable so applying the advisor's read (sized for a market entry) can reset it. */
		orderType?: TicketOrderType;
		/** Protective tab and trailing fields; bindable so the planned-trailing notice can fill them (D10). */
		protectTab?: ProtectTab;
		protTrail?: string;
		protTrailPct?: string;
		trailMode?: 'price' | 'pct';
		onSubmit: (d: OrderDraft) => void;
		onPick?: (set: (price: number) => void) => void;
	} = $props();

	const s = $derived(session.state!);
	const L = $derived(i18n.locale);
	const k = $derived(costsOf(s.contract, s.liquidity));
	// A liquidity shock in force on the bar orders placed now fill on: the estimates above already use its costs.
	const shock = $derived(s.liquidity ? shockFactors(s.contract, s.liquidity) : null);
	const tick = $derived(num(s.contract.tickSize));
	const lot = $derived(num(s.contract.lotSize));
	// Liquidation is checked at the mark; orders are placed, checked and filled at the last price.
	const mark = $derived(num(s.account.mark));
	const last = $derived(lastPrice(s));
	const cap = $derived(s.contract.leverageCap);
	const posQty = $derived(num(s.position.qty));
	const atr = $derived.by(() => {
		const v = s.indicators.atr14[s.indicators.atr14.length - 1];
		return v != null ? Number(v) : null;
	});
	const bars = $derived(s.bars.map((b) => b.slice(0, 5).map(Number) as BarTuple));

	// Typed amounts per sizing mode, read with either decimal mark ("0,5" or "0.5").
	let riskInput = $state('1');
	let notionalInput = $state('1000');
	let equityInput = $state('10');
	// Typed sizing amounts; the notional is in credits, so "1.000" in PT-BR is one thousand.
	const AMOUNT: Record<SizeBy, () => number> = {
		qty: () => parseDecimal(qtyInput),
		notional: () => parseAmount(notionalInput, L),
		equity: () => parseDecimal(equityInput),
		risk: () => parseDecimal(riskInput)
	};
	let priceInput = $state('');

	// The field follows the account leverage only when that (or the cap) changes. Every command returns
	// a new state, and re-syncing on each one (e.g. asking the advisor) threw away a leverage the player
	// had typed, so the order went out at the old leverage.
	let levSeen = '';
	$effect(() => {
		const key = `${s.leverage}/${cap}`;
		if (key === levSeen) return;
		levSeen = key;
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

	const entry = $derived(orderType === 'market' ? estimatedMarketEntry(side, last, k) : parseDecimal(priceInput) || last);
	const stop = $derived(stopInput ? parseDecimal(stopInput) : null);
	const qty = $derived(
		sizedQty(sizeBy, { equity: num(s.account.equity), entry, stop, qty: parseDecimal(qtyInput) || 0, amount: AMOUNT[sizeBy]() || 0 }, k, lot)
	);
	const qtyStr = $derived(qtyToLot(qty, lot));
	// The field can hold anything typed; orders and estimates use the value clamped to the mission cap.
	const lev = $derived(Math.min(Math.max(1, Math.round(leverage || 1)), cap));
	// Liquidation of the position this order leads to (an add, a reduction or a reversal), not of the order alone.
	const openPos = $derived({ qty: posQty, entry: num(s.position.entry), margin: num(s.position.margin), leverage: s.leverage });
	const est = $derived(estimate({ side, qty, entry, leverage: lev, stop }, k, openPos));
	const shortBlocked = $derived(!s.contract.allowShort && side === 'sell' && posQty <= 0);
	const increases = $derived(posQty === 0 || posQty > 0 === (side === 'buy') || qty > Math.abs(posQty));
	// Against an open position the order reduces, closes or reverses it rather than opening a new trade.
	const opposing = $derived(opposingEffect(posQty, side, qty, lot));
	// Same check as the engine on placement: margin + fee for the part that adds exposure vs available.
	const addedQty = $derived(posQty === 0 || posQty > 0 === (side === 'buy') ? qty : Math.max(0, qty - Math.abs(posQty)));
	const needMargin = $derived(requiredMargin(addedQty, entry, lev, k));
	const cantAfford = $derived(addedQty > 0 && needMargin > num(s.account.available));
	// Policy v16: the position this order leads to sits in a margin tier that may cap leverage lower.
	const notionalAfter = $derived(
		(posQty !== 0 && posQty > 0 === (side === 'buy') ? Math.abs(posQty) * num(s.position.entry) * k.multiplier : 0) +
			addedQty * entry * k.multiplier
	);
	const tier = $derived(marginTier(k, notionalAfter));
	const aboveTier = $derived(addedQty > 0 && lev > tier.maxLeverage);
	const tp = $derived(tpInput ? parseDecimal(tpInput) : null);
	const stopWrongSide = $derived(stop != null && !stopOnLosingSide(side, entry, stop));
	const tpWrongSide = $derived(tp != null && !targetOnWinningSide(side, entry, tp));
	const needsPrice = $derived(orderType !== 'market');
	// A stop beyond liquidation needs an explicit confirmation when the mission asks for one (engine
	// MissionRules, task 4.1); the confirmation belongs to the exact order it was given for.
	let beyondConfirmed = $state(false);
	const beyondKey = $derived(`${side}|${qty}|${lev}|${stopInput}|${orderType}|${priceInput}`);
	$effect(() => {
		void beyondKey;
		beyondConfirmed = false;
	});
	const needsBeyondConfirm = $derived(est.stopBeyondLiquidation && s.missionRules.confirmStopBeyondLiq);
	// Time in force (policy v18, design D6): only on an engine build that takes it.
	let tifChoice = $state<TifChoice>('gtc');
	let tifBarsInput = $state('3');
	const tif = $derived(session.orderRulesV18 ? tifOrder(tifChoice, parseDecimal(tifBarsInput), s.stepSeconds, barsLeft(s)) : { tif: 'gtc' as const });
	// "Protect with" (design D10): a fixed stop rides on the order as a bracket; a trailing stop is
	// planned now and offered after the fill (the engine attaches no trailing stop to an entry).
	let protectWith = $state<'fixed' | 'trail'>('fixed');
	let planTrail = $state('');
	let planTrailPct = $state('');
	let planTrailMode = $state<'price' | 'pct'>('price');
	const plannedTrail = $derived<PlannedTrail | null>(
		protectWith !== 'trail' ? null : planTrailMode === 'pct' ? (planTrailPct ? { mode: 'pct', value: planTrailPct } : null) : planTrail ? { mode: 'price', value: planTrail } : null
	);
	const canSubmit = $derived(
		!session.busy &&
			!('error' in tif) &&
			qty > 0 &&
			!shortBlocked &&
			!cantAfford &&
			!aboveTier &&
			!stopWrongSide &&
			!tpWrongSide &&
			(!needsBeyondConfirm || beyondConfirmed) &&
			(!needsPrice || parseDecimal(priceInput) > 0)
	);

	function submit() {
		if (!canSubmit) return;
		onSubmit({
			side,
			type: orderType,
			qty: qtyStr,
			price: needsPrice ? priceToTick(parseDecimal(priceInput), tick) : null,
			leverage: lev,
			stop: stopInput ? priceToTick(parseDecimal(stopInput), tick) : null,
			takeProfit: tpInput ? priceToTick(parseDecimal(tpInput), tick) : null,
			estimate: est,
			increases,
			required: needMargin,
			...('error' in tif || tif.tif === 'gtc' ? {} : tif),
			...(plannedTrail ? { plannedTrail } : {})
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
		const atrPart = d.atr != null ? ` · ${fmtNum(d.atr, L, 1)}×ATR` : '';
		return `${fmtPrice(String(d.abs), L)} · ${fmtNum(d.pct, L)}%${atrPart}`;
	}

	function targetHint(sd: OrderSideName, from: number, stopAt: number | null, level: number | null): string | null {
		const base = stopHint(from, level);
		const rr = rewardRisk(sd, from, stopAt, level);
		return base && rr != null ? `${base} · R:R 1:${fmtNum(rr, L, 1)}` : base;
	}

	const stopWarn = $derived(stopWrongSide ? t('level.stopSide', { side: side === 'buy' ? '<' : '>', price: fmtPrice(String(entry), L) }) : null);
	const tpWarn = $derived(tpWrongSide ? t('level.tpSide', { side: side === 'buy' ? '>' : '<', price: fmtPrice(String(entry), L) }) : null);

	// Protection for an existing position.
	let protStop = $state('');
	let protTp = $state('');
	const trailRate = $derived(trailMode === 'pct' && protTrailPct ? trailRateFromPercent(parseDecimal(protTrailPct)) : null);
	const trailSet = $derived(trailMode === 'pct' ? trailRate != null : !!protTrail);
	const posSide = $derived<OrderSideName>(posQty > 0 ? 'buy' : 'sell');
	const posEntry = $derived(num(s.position.entry));
	const protStopN = $derived(protStop ? parseDecimal(protStop) : null);
	const protTpN = $derived(protTp ? parseDecimal(protTp) : null);
	const protStopWarn = $derived(
		protStopN != null && !stopOnLosingSide(posSide, last, protStopN) ? t('level.protStopSide', { price: fmtPrice(String(last), L) }) : null
	);
	// A protective stop for the open position beyond its liquidation price also asks for confirmation.
	const liqNow = $derived(s.account.liquidationPrice ? num(s.account.liquidationPrice) : null);
	const protBeyond = $derived(
		protStopN != null && liqNow != null && (posQty > 0 ? protStopN <= liqNow : protStopN >= liqNow) && s.missionRules.confirmStopBeyondLiq
	);
	let protConfirmed = $state(false);
	$effect(() => {
		void protStop;
		protConfirmed = false;
	});
	const protTpWarn = $derived(
		protTpN != null && !targetOnWinningSide(posSide, last, protTpN) ? t('level.protTpSide', { price: fmtPrice(String(last), L) }) : null
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
		if (protStop) await session.placeOrder('stop_market', exit, size, priceToTick(parseDecimal(protStop), tick), true);
		if (protTp) await session.placeOrder('take_profit', exit, size, priceToTick(parseDecimal(protTp), tick), true);
		if (trailSet) await session.placeOrder('trailing_stop', exit, size, null, true, trailExtras());
		protStop = '';
		protTp = '';
		protTrail = '';
		protTrailPct = '';
	}

	// A trailing stop by percent of the close (policy v16) or by a fixed price distance.
	function trailExtras() {
		return trailMode === 'pct' ? { trailRate } : { trail: priceToTick(parseDecimal(protTrail), tick) };
	}

	const PROTECT_TABS: readonly ProtectTab[] = ['stop', 'trail', 'target'];
	const filled = $derived<Record<ProtectTab, boolean>>({ stop: !!protStop, trail: trailSet, target: !!protTp });
	// The rule in words with the engine's first trigger (exact fixed point, design D10).
	const trailRule = $derived.by(() => {
		const exit: OrderSideName = posQty > 0 ? 'sell' : 'buy';
		const close = String(last);
		if (trailMode === 'pct' && trailRate) {
			return t('ticket.trailRule', { price: fmtPrice(trailTrigger(exit, close, { rate: trailRate }, s.contract.tickSize), L) });
		}
		if (trailMode === 'price' && protTrail) {
			const dist = priceToTick(parseDecimal(protTrail), tick);
			return dist ? t('ticket.trailRule', { price: fmtPrice(trailTrigger(exit, close, { distance: dist }, s.contract.tickSize), L) }) : t('ticket.trailExplain');
		}
		return t('ticket.trailExplain');
	});
</script>

<section class="panel ticket" aria-label={t('ticket.title')} data-coach="ticket">
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
			<Term id="market-order">{t('ticket.type')}</Term>
			<select bind:value={orderType}>
				<option value="market">{t('ticket.market')}</option>
				<option value="limit">{t('ticket.limit')}</option>
				<option value="stop_market">{t('ticket.stop')}</option>
			</select>
		</label>
		<label>
			<Term id="leverage">{t('ticket.leverage')}</Term> (≤ {cap}x)
			<input type="number" min="1" max={cap} step="1" bind:value={leverage} onchange={() => (leverage = lev)} />
		</label>
		{#if needsPrice}
			<label class="full">
				{orderType === 'limit' ? t('ticket.price') : t('ticket.trigger')}
				<input inputmode="decimal" bind:value={priceInput} placeholder={fmtPrice(String(last), L)} />
			</label>
		{/if}
		<label>
			{t('ticket.sizeBy')}
			<select bind:value={sizeBy}>
				<option value="risk">{t('ticket.sizeRisk')}</option>
				<option value="qty">{t('ticket.sizeQty')}</option>
				<option value="notional">{t('ticket.sizeNotional')}</option>
				<option value="equity">{t('ticket.sizeEquity')}</option>
			</select>
		</label>
		{#if sizeBy === 'risk'}
			<label>
				<Term id="risk-budget">{t('ticket.riskPct')}</Term>
				<input inputmode="decimal" bind:value={riskInput} data-testid="ticket-risk" />
			</label>
		{:else if sizeBy === 'notional'}
			<label>
				{t('ticket.notional')}
				<input inputmode="decimal" bind:value={notionalInput} data-testid="ticket-notional" />
			</label>
		{:else if sizeBy === 'equity'}
			<label>
				{t('ticket.equityPct')}
				<input inputmode="decimal" bind:value={equityInput} data-testid="ticket-equity" />
			</label>
		{:else}
			<label>
				{t('ticket.qty')}
				<input inputmode="decimal" bind:value={qtyInput} />
			</label>
		{/if}
	</div>
	<div class="protect-with" role="radiogroup" aria-label={t('ticket.protectWith')} data-testid="ticket-protect-with">
		<span class="cap">{t('ticket.protectWith')}</span>
		<button type="button" role="radio" aria-checked={protectWith === 'fixed'} class:active={protectWith === 'fixed'} onclick={() => (protectWith = 'fixed')}>{t('ticket.protectFixed')}</button>
		<button type="button" role="radio" aria-checked={protectWith === 'trail'} class:active={protectWith === 'trail'} onclick={() => ((protectWith = 'trail'), (stopInput = ''))} data-testid="ticket-protect-trail">{t('ticket.protectTrail')}</button>
	</div>
	{#if protectWith === 'trail'}
		<TrailField bind:distance={planTrail} bind:pct={planTrailPct} bind:mode={planTrailMode} {tick} chips={trailChips} exit={side === 'buy' ? 'sell' : 'buy'} close={last} testid="ticket-plan-trail" />
		<p class="small info" data-testid="ticket-trail-planned"><Term id="trailing-stop">{t('ticket.protectTrail')}</Term> {t('ticket.trailPlanned')}</p>
	{/if}
	<div class="levels">
		{#if protectWith === 'fixed'}
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
		{/if}
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
	{#if session.orderRulesV18}
		<TifField bind:choice={tifChoice} bind:barsInput={tifBarsInput} type={orderType} stepSeconds={s.stepSeconds} step={s.step} left={barsLeft(s)} testid="ticket-tif" />
	{/if}
	{#if sizeBy === 'risk' && stop == null}
		<p class="small attention">↳ {t('ticket.riskNeedsStop')}</p>
	{/if}
	{#if shock}
		<p class="warnbox" role="status" data-testid="ticket-shock">
			⚠ {t('ticket.shock', { cost: fmtNum(shock.cost, L, 1), capacity: fmtPct(shock.capacity, L, 0) })}
		</p>
	{/if}

	<dl class="kv est">
		<dt>{t('ticket.qty')}</dt><dd data-testid="ticket-est-qty">{fmtQty(qtyStr, L)}</dd>
		<dt><Term id="notional">{t('ticket.estNotional')}</Term></dt><dd data-testid="ticket-est-notional">{fmtMoney(est.notional, L)}</dd>
		<dt>{t('ticket.estEntry')}</dt><dd data-testid="ticket-est-entry">{fmtPrice(String(entry), L)}</dd>
		<dt><Term id="initial-margin">{t('ticket.estMargin')}</Term></dt><dd>{fmtMoney(est.margin, L)}</dd>
		<dt><Term id="fee">{t('ticket.estFee')}</Term></dt><dd>{fmtMoney(est.fee, L)}</dd>
		<dt><Term id="liquidation-price">{t('ticket.estLiq')}</Term></dt><dd class="down" data-testid="ticket-est-liq">{est.liquidation ? fmtPrice(String(est.liquidation), L) : '—'}</dd>
		<dt data-coach="risk"><Term id="stop-loss">{t('ticket.estRisk')}</Term></dt><dd>{est.plannedRisk != null ? fmtMoney(est.plannedRisk, L) : '—'}</dd>
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
	{#if opposing}
		<p class="warnbox" role="status" data-testid="ticket-opposing">
			⚠ {t(`ticket.opposing.${opposing.kind}`, { open: fmtQty(String(opposing.open), L), after: fmtQty(String(opposing.after), L) })}
		</p>
	{/if}
	{#if cantAfford}
		<p class="warnbox" role="alert" data-testid="ticket-cant-afford">
			⚠ {t('ticket.cantAfford', { need: fmtMoney(needMargin, L), avail: fmtMoney(s.account.available, L) })}
		</p>
	{/if}
	{#if aboveTier}
		<p class="warnbox" role="alert" data-testid="ticket-above-tier">
			⚠ {t('ticket.aboveTier', { notional: fmtMoney(notionalAfter, L), max: tier.maxLeverage, mmr: fmtPct(tier.mmr, L, 1) })}
		</p>
	{/if}
	{#if est.stopBeyondLiquidation}
		<div class="warnbox" role="alert" data-testid="ticket-beyond-liq">
			⚠ {t('ticket.stopBeyondLiq')}
			{#if needsBeyondConfirm}
				<label class="confirm">
					<input type="checkbox" bind:checked={beyondConfirmed} data-testid="ticket-confirm-beyond" />
					{t('ticket.confirmBeyond')}
				</label>
			{/if}
		</div>
	{/if}
	{#if stop == null && qty > 0}<p class="small attention">{t('ticket.noStop')}</p>{/if}
	<p class="small muted">{t('ticket.attachNote')}</p>

	<button class="primary submit {side}" onclick={submit} disabled={!canSubmit}>
		{side === 'buy' ? '▲' : '▼'} {t('ticket.submit')} <kbd>Enter</kbd>
	</button>

	{#if posQty !== 0}
		<hr />
		<h3>{t('ticket.protect')}</h3>
		{#if !hasStop}<p class="warnbox" role="status">⚠ {t('ticket.unprotected')}</p>{/if}
		<div class="tabs" role="tablist" aria-label={t('ticket.protect')} data-testid="protect-tabs">
			{#each PROTECT_TABS as tab (tab)}
				<button
					role="tab"
					aria-selected={protectTab === tab}
					class:primary={protectTab === tab}
					onclick={() => (protectTab = tab)}
					data-testid={`protect-tab-${tab}`}
				>{t(`ticket.tab.${tab}`)}{filled[tab] ? ' •' : ''}</button>
			{/each}
		</div>
		<div class="levels" role="tabpanel">
			{#if protectTab === 'stop'}
			<div class="full">
			<LevelField
				testid="protect-stop"
				label={t('ticket.stopLoss')}
				bind:value={protStop}
				{tick}
				chips={stopChips(posSide, last)}
				hint={stopHint(posEntry, protStopN)}
				warn={protStopWarn}
				{onPick}
			/>
			</div>
			{:else if protectTab === 'target'}
			<div class="full">
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
			</div>
			{:else}
			<div class="full">
				<TrailField bind:distance={protTrail} bind:pct={protTrailPct} bind:mode={trailMode} {tick} chips={trailChips} exit={posQty > 0 ? 'sell' : 'buy'} close={last} />
				<p class="small muted" data-testid="protect-trail-rule">{trailRule}</p>
			</div>
			{/if}
		</div>
		<div class="row">
			{#if protBeyond}
				<div class="warnbox" role="alert" data-testid="protect-beyond-liq">
					⚠ {t('ticket.stopBeyondLiq')}
					<label class="confirm"><input type="checkbox" bind:checked={protConfirmed} data-testid="protect-confirm-beyond" /> {t('ticket.confirmBeyond')}</label>
				</div>
			{/if}
			<button
				onclick={protect}
				disabled={session.busy || !!protStopWarn || !!protTpWarn || (protBeyond && !protConfirmed) || (!protStop && !protTp && !trailSet)}
			>
				{t('ticket.protectSubmit')}
			</button>
			<button class="warn" onclick={() => session.closePosition()} disabled={session.busy}>✕ {t('ticket.close')} <kbd>{keyLabel(keymap.map.close)}</kbd></button>
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
	/* The side not chosen is dashed, not faded: a faded label failed WCAG AA contrast (axe, coach-mark e2e). */
	.sides button:not(.active) {
		border-style: dashed;
	}
	.grid {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.35rem 0.5rem;
	}
	/* A caption that wraps (e.g. "Exposição (% patrimônio)" at 1024px) keeps its field level with the neighbour's. */
	.grid > label {
		justify-content: flex-end;
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
	.tabs {
		display: grid;
		grid-template-columns: repeat(3, 1fr);
		gap: 0.25rem;
	}
	.protect-with {
		display: flex;
		align-items: center;
		gap: 0.3rem;
		flex-wrap: wrap;
	}
	.protect-with .cap {
		font-size: 0.78rem;
		color: var(--muted);
	}
	.protect-with button {
		font-size: 0.78rem;
		padding: 0.15rem 0.45rem;
		border-style: dashed;
	}
	.protect-with button.active {
		border-style: solid;
		background: rgba(57, 255, 136, 0.12);
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
		display: flex;
		flex-direction: column;
		gap: 0.15rem;
	}
	/* The stop-beyond-liquidation confirmation: checkbox beside its sentence, not on a line of its own. */
	label.confirm {
		flex-direction: row;
		align-items: flex-start;
		gap: 0.4rem;
		margin-top: 0.3rem;
		color: inherit;
	}
	label.confirm input {
		margin: 0.1rem 0 0;
		flex-shrink: 0;
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
	/* Full colour, not faded: a faded key on the red sell button failed WCAG AA contrast (axe, a11y e2e). */
	kbd {
		font-size: 0.7rem;
	}
</style>
