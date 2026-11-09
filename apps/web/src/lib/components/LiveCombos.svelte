<script lang="ts">
	// Live combos: roll and adjust (add-practice-and-advanced-strategies D3, W1-W6). A roll closes every
	// held leg of the near expiry and opens the same structure on a later one; an adjustment closes one
	// leg or adds a hedge leg. Both are one submission of ordinary orders under the combo's id (tagged
	// roll/adjust, ignored by replay), ordered buys, perpetual, sells; previewed by the engine's dry run and
	// the what-if of the holdings after the change. Closing a leg that leaves a short uncovered asks first.
	import type { ComboPreview, WhatIfResult } from '#lib/engine/types.ts';
	import type { Analysis } from '#lib/engine/protocol.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { comboHoldings, comboStatus, nextSubmissionNumber, type ComboStatus } from '#lib/game/combos.ts';
	import { fmtMoney, fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { i18n, t, tCount } from '#lib/game/i18n.svelte.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import { comboLegs, defaultLimit, hedgeOrder, premiumText, rollOrders, rollTargets, whatifLegs, type OrderLeg } from '#lib/game/strategies.ts';
	import { analyticsOf } from '#lib/game/whatifLab.ts';
	import type { ComboDraft } from './StrategyBuilder.svelte';
	import ConfirmDialog from './ConfirmDialog.svelte';
	import Term from './Term.svelte';

	let { session, onSubmit }: { session: GameSession; onSubmit: (d: ComboDraft) => void } = $props();

	const L = $derived(i18n.locale);
	const game = $derived(session.state!);
	const chain = $derived(game.options.chain);
	const live = $derived(session.combos.map((r) => comboStatus(game.events, r)).filter((st) => comboHoldings(st).some((h) => h.series !== 0)));

	let selected = $state<string | null>(null);
	let mode = $state<'roll' | 'adjust' | null>(null);
	let to = $state(0);
	let recenter = $state(false);
	let limitIoc = $state(false);
	let adjustment = $state<OrderLeg[] | null>(null);
	let adjustKind = $state<'close' | 'hedge'>('close');
	let preview = $state<Analysis<ComboPreview> | null>(null);
	let before = $state<WhatIfResult | null>(null);
	let after = $state<WhatIfResult | null>(null);
	let confirming = $state(false);
	let ticket = 0;

	const status = $derived(live.find((st) => st.record.id === selected) ?? null);
	const held = $derived(status ? comboHoldings(status) : []);
	const options = $derived(held.filter((h) => h.series !== 0));
	const from = $derived(Math.min(...options.map((h) => chain.find((q) => q.id === h.series)?.expiryStep ?? Infinity)));
	const targets = $derived(Number.isFinite(from) ? rollTargets(chain, from) : []);
	$effect(() => {
		if (!targets.includes(to)) to = targets[0] ?? 0;
	});

	const tick = $derived(Number(game.options.premiumTick));
	function withLimits(orders: OrderLeg[]): OrderLeg[] {
		return orders.map((o) => {
			const q = chain.find((x) => x.id === o.series);
			return q ? { ...o, premium: premiumText(defaultLimit(q, o.side, tick), tick) } : o;
		});
	}

	const rollPlan = $derived(mode === 'roll' && status && to ? rollOrders(game, options, to, recenter, lastPrice(game)) : null);
	const orders = $derived<OrderLeg[] | null>(mode === 'roll' ? (rollPlan ? (limitIoc ? withLimits(rollPlan.orders) : rollPlan.orders) : null) : adjustment);
	const tif = $derived(mode === 'roll' && limitIoc ? { tif: 'ioc' as const } : undefined);

	// The engine's dry run of the orders and the what-if of the holdings before and after them.
	$effect(() => {
		const o = orders;
		void game.digest;
		const mine = ++ticket;
		if (!o || o.length === 0) {
			preview = null;
			after = null;
			return;
		}
		void Promise.all([
			session.comboPreview({ legs: comboLegs(o, tif) }),
			session.runWhatIf({ holdings: true, legs: [] }),
			session.runWhatIf({ holdings: true, legs: whatifLegs(o, null) })
		]).then(([p, b, a]) => {
			if (mine !== ticket) return;
			preview = p;
			before = b.ok ? b.value : null;
			after = a.ok ? a.value : null;
		});
	});

	function name(st: ComboStatus): string {
		return t(`strategy.${st.record.strategy}`);
	}

	function legName(series: number, qty: number | string): string {
		const q = chain.find((x) => x.id === series);
		const n = Number(qty);
		const what = series === 0 ? t('lab.perp') : q ? `${q.kind === 'call' ? 'C' : 'P'} ${fmtPrice(q.strike, L)} @${q.expiryStep}` : `OPT#${series}`;
		return `${n >= 0 ? '+' : '−'}${fmtQty(String(Math.abs(n)), L)} ${what}`;
	}
	const orderName = (o: OrderLeg) => `${o.side === 'buy' ? t('opt.buy') : t('opt.sell')} ${legName(o.series, o.qty).slice(1)}`;

	function open(st: ComboStatus, m: 'roll' | 'adjust') {
		selected = st.record.id;
		mode = m;
		adjustment = null;
	}

	function closeLeg(h: { series: number; qty: number }) {
		const lot = h.series === 0 ? Number(game.contract.lotSize) : Number(game.options.lotSize);
		const decimals = Math.max(0, Math.round(-Math.log10(lot)));
		adjustment = [{ series: h.series, side: h.qty > 0 ? 'sell' : 'buy', qty: Math.abs(h.qty).toFixed(decimals) }];
		adjustKind = 'close';
	}

	function addHedge() {
		const delta = before?.risk ? Number(before.risk.deltaUnits) : 0;
		const h = hedgeOrder(game, delta);
		adjustment = h ? [h] : [];
		adjustKind = 'hedge';
	}

	// Design D3: closing a leg that leaves a short with no maximum loss asks for a confirmation naming it.
	// A roll that does the same (a short rolled past the long that covered it) asks too.
	const uncovered = $derived.by(() => {
		const removing = mode === 'roll' || (mode === 'adjust' && adjustKind === 'close');
		if (!removing || !orders?.length || !before || !after) return null;
		const was = analyticsOf(before)?.maxLoss;
		const now = analyticsOf(after)?.maxLoss;
		if (was == null || now != null) return null;
		if (mode === 'roll') {
			const sale = orders.findLast((o) => o.side === 'sell');
			return sale ? orderName(sale) : '';
		}
		const closing = new Set(orders.map((o) => o.series));
		const short = options.find((h) => h.qty < 0 && !closing.has(h.series));
		return short ? legName(short.series, short.qty) : '';
	});

	function send() {
		confirming = false;
		if (!status || !orders?.length || !mode) return;
		const n = nextSubmissionNumber(status.record, mode);
		const a = after ? analyticsOf(after) : null;
		const risk = a?.maxLoss != null ? Math.max(0, Number(a.maxLoss)) : preview?.ok ? Number(preview.value.required) : null;
		onSubmit({
			// rollOrders is already buys, then sells; an adjustment is one leg (design D3's one ordering rule).
			orders,
			tag: { combo: status.record.id, strategy: status.record.strategy, [mode]: n },
			label: t(mode === 'roll' ? 'roll.title' : 'adjust.title', { name: name(status) }),
			risk,
			unbounded: a != null && a.maxLoss == null,
			...(tif ? { tif } : {})
		});
		mode = null;
		adjustment = null;
	}
</script>

{#if live.length}
	<section class="live" aria-labelledby="live-title" data-testid="live-combos">
		<h3 id="live-title">{t('combo.live')} · <Term id="roll">{t('roll.button')}</Term></h3>
		<ul>
			{#each live as st (st.record.id)}
				<li>
					<span>{name(st)} · {comboHoldings(st).map((h) => legName(h.series, h.qty)).join(' ')}</span>
					<button class="mini" onclick={() => open(st, 'roll')} data-testid={`roll-${st.record.id}`}>⟳ {t('roll.button')}</button>
					<button class="mini" onclick={() => open(st, 'adjust')} data-testid={`adjust-${st.record.id}`}>{t('adjust.button')}</button>
				</li>
			{/each}
		</ul>
		{#if status && mode === 'roll'}
			<div class="panel-in" data-testid="roll-panel">
				<h4>{t('roll.title', { name: name(status) })}</h4>
				{#if targets.length === 0}
					<p class="muted small">{t('roll.none')}</p>
				{:else}
					<label>{t('roll.to')}
						<select bind:value={to} data-testid="roll-to">
							{#each targets as e (e)}<option value={e}>{t('builder.expiryAt', { step: e, n: e - game.step })}</option>{/each}
						</select>
					</label>
					<label class="check"><input type="checkbox" bind:checked={recenter} data-testid="roll-recenter" /> {t('roll.recenter')}</label>
					{#if session.orderRulesV18}<label class="check"><input type="checkbox" bind:checked={limitIoc} data-testid="roll-limit" /> {t('roll.limitIoc')}</label>{/if}
				{/if}
			</div>
		{:else if status && mode === 'adjust'}
			<div class="panel-in" data-testid="adjust-panel">
				<h4>{t('adjust.title', { name: name(status) })}</h4>
				<ul>
					{#each held as h (h.series)}
						<li>{legName(h.series, h.qty)} <button class="mini" onclick={() => closeLeg(h)} data-testid="adjust-close-leg">{t('adjust.closeLeg')}</button></li>
					{/each}
				</ul>
				<button class="mini" onclick={addHedge} data-testid="adjust-add-hedge">+ {t('adjust.addHedge')}: {t('adjust.hedgePerp')}</button>
				{#if adjustment && adjustment.length === 0}<p class="muted small">{t('portfolio.hedgeNone')}</p>{/if}
			</div>
		{/if}
		{#if mode && orders?.length && preview?.ok}
			{@const p = preview.value}
			<ol class="small" data-testid="live-orders">
				{#each orders as o, i (i)}
					<li class:down={!p.legs[i]?.accepted}>{orderName(o)}{o.premium ? ` @ ${fmtPrice(o.premium, L)}` : ''}</li>
				{/each}
			</ol>
			<dl class="kv small" data-testid="live-totals">
				<dt>{t(mode === 'roll' ? 'roll.net' : 'builder.credit')}</dt><dd data-testid="roll-net">{fmtMoney(p.netPremium, L, true)}</dd>
				{#if after}
					{@const a = analyticsOf(after)}
					<dt>{t('adjust.after')}: {t('lab.maxLoss')}</dt>
					<dd data-testid="live-after-max-loss">{a?.maxLoss != null ? fmtMoney(a.maxLoss, L) : t('lab.unbounded')}</dd>
				{/if}
			</dl>
			<button
				class="primary"
				disabled={session.busy || !p.accepted}
				onclick={() => (uncovered != null ? (confirming = true) : send())}
				data-testid="live-send"
			>{t(mode === 'roll' ? 'roll.send' : 'adjust.send')}</button>
			<span class="muted small">{tCount('combo.label', orders.length, { name: name(status!) })}</span>
		{/if}
	</section>
{/if}

{#if confirming && uncovered != null}
	<ConfirmDialog
		title={t('adjust.uncoveredTitle')}
		message={t('adjust.uncovered', { leg: uncovered })}
		confirmLabel={t('adjust.uncoveredYes')}
		cancelLabel={t('orders.linkNo')}
		onConfirm={send}
		onCancel={() => (confirming = false)}
	/>
{/if}

<style>
	.live {
		border-top: 1px solid var(--line);
		padding-top: 0.4rem;
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
	}
	h3,
	h4 {
		margin: 0;
		font-size: 0.85rem;
	}
	ul,
	ol {
		margin: 0;
		padding-left: 1.4rem;
		font-size: 0.8rem;
	}
	dd {
		white-space: nowrap;
	}
	li {
		display: flex;
		flex-wrap: wrap;
		gap: 0.3rem;
		align-items: center;
	}
	ol li {
		display: list-item;
	}
	.panel-in {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
		display: flex;
		flex-direction: column;
	}
	label.check {
		flex-direction: row;
		align-items: center;
		gap: 0.3rem;
	}
	.mini {
		padding: 0.1rem 0.4rem;
		min-height: 24px;
		font-size: 0.75rem;
	}
	.small {
		font-size: 0.75rem;
	}
</style>
