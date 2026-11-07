<script lang="ts" module>
	import type { ComboTag } from '#lib/game/combos.ts';
	import type { OrderLeg } from '#lib/game/strategies.ts';

	/** A combo ready to send: legs in submission order, its tag, name and the risk for the plan. */
	export interface ComboDraft {
		orders: OrderLeg[];
		tag: ComboTag;
		label: string;
		risk: number | null;
		/** No maximum loss at expiry: `risk` is then the margin required, not a worst case. */
		unbounded: boolean;
	}
</script>

<script lang="ts">
	// Strategy builder (strategy-builder spec): templates from the chain, free legs on one expiry, the
	// engine's analysis (what-if at the prices the legs would cross, and a dry run that changes nothing)
	// and submission as ordinary market orders, longs first (design D3).
	import type { ComboPreview, WhatIfLegInput, WhatIfResult } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import type { Analysis } from '#lib/engine/protocol.ts';
	import { fmtMoney, fmtNum, fmtPrice, fmtQty, parseDecimal } from '#lib/game/format.ts';
	import { i18n, t, tCount } from '#lib/game/i18n.svelte.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import { nextComboId } from '#lib/game/combos.ts';
	import { rejectionText } from '#lib/game/rejections.ts';
	import {
		COVERS_LONG,
		MAX_OPTION_LEGS,
		TEMPLATES,
		comboLegs,
		coverContracts,
		expiriesOf,
		heldLong,
		legsProblem,
		orderLegs,
		positionLegs,
		seriesFor,
		strategyOf,
		strikesOf,
		templateLegs,
		templateShortable,
		templateWrites,
		whatifLegs,
		writesOptions,
		type BuilderLeg,
		type LegKind,
		type StrategyId,
		type TemplateId
	} from '#lib/game/strategies.ts';
	import { unboundedAbove } from '#lib/game/whatifLab.ts';
	import PayoffChart from './PayoffChart.svelte';
	import Term from './Term.svelte';
	import WhatIfTable from './WhatIfTable.svelte';

	let {
		session,
		onSubmit,
		onLab
	}: { session: GameSession; onSubmit: (d: ComboDraft) => void; onLab: (legs: WhatIfLegInput[]) => void } = $props();

	const L = $derived(i18n.locale);
	const game = $derived(session.state!);
	const chain = $derived(game.options.chain);
	const expiries = $derived(expiriesOf(chain));
	const writeAllowed = $derived(game.missionRules.permissions.optionsWrite);
	const f = $derived(lastPrice(game));

	let expiry = $state(0);
	let template = $state<TemplateId | null>(null);
	let short = $state(false);
	let nInput = $state('1');
	let legs = $state<BuilderLeg[]>([]);
	let strategy = $state<StrategyId>('custom');
	let useLong = $state(false);
	let whatif = $state<Analysis<WhatIfResult> | null>(null);
	let preview = $state<Analysis<ComboPreview> | null>(null);
	let pending = $state(false);
	let ticket = 0;

	$effect(() => {
		if (!expiries.includes(expiry)) expiry = expiries[0] ?? 0;
	});

	const strikes = $derived(strikesOf(chain, expiry));
	const n = $derived(Math.max(0, parseDecimal(nInput) || 0));
	const held = $derived(heldLong(game));
	const covers = $derived(useLong && held != null);
	const sent = $derived(covers ? legs.filter((l) => !(l.kind === 'perp' && l.side === 'buy')) : legs);
	const orders = $derived(orderLegs(game, expiry, sent));
	const problem = $derived(problemOf());

	function problemOf(): ReturnType<typeof legsProblem> | 'unlisted' | 'write_locked' {
		const p = legsProblem(sent);
		if (p) return p;
		if (!orders) return 'unlisted';
		return writesOptions(sent) && !writeAllowed ? 'write_locked' : null;
	}

	// Analysis: the engine's what-if of the legs as they would fill, and the dry run of the orders.
	$effect(() => {
		const o = orders;
		void game.digest;
		const mine = ++ticket;
		if (!o || o.length === 0 || legsProblem(sent)) {
			whatif = preview = null;
			return;
		}
		pending = true;
		const heldLeg = covers ? held : null;
		void Promise.all([session.runWhatIf({ legs: whatifLegs(o, heldLeg) }), session.comboPreview({ legs: comboLegs(o) })]).then(([w, p]) => {
			if (mine !== ticket) return;
			whatif = w;
			preview = p;
			pending = false;
		});
	});

	function pick(id: TemplateId) {
		template = id;
		strategy = strategyOf(id, short);
		// A covered call or protective put on a held long covers that long, all of it, by default.
		useLong = held != null && COVERS_LONG.has(id);
		if (useLong && held) nInput = fmtNum(coverContracts(game, held), L, 1);
		legs = templateLegs(id, strikes, f, n || 1, short) ?? [];
	}

	function toggleShort() {
		short = !short;
		if (template && templateShortable(template)) pick(template);
	}

	function edit(i: number, patch: Partial<BuilderLeg>, structural = false) {
		legs = legs.map((l, j) => (j === i ? { ...l, ...patch } : l));
		if (structural) strategy = 'custom';
	}

	function setKind(i: number, kind: LegKind) {
		edit(i, { kind, strike: kind === 'perp' ? null : (legs[i].strike ?? strikes[0] ?? null) }, true);
	}

	function addLeg() {
		legs = [...legs, { kind: 'call', side: 'buy', strike: strikes.find((k) => k >= f) ?? strikes[0] ?? null, qty: n || 1 }];
		strategy = 'custom';
	}

	const writeLocked = (id: TemplateId) => templateWrites(id, short) && !writeAllowed;
	const comboOk = $derived(preview?.ok ? preview.value.accepted : true);
	const canSubmit = $derived(!problem && !pending && !session.busy && orders != null && orders.length > 0 && comboOk);

	function risk(): number | null {
		const e = whatif?.ok ? whatif.value.expiry : null;
		if (e?.maxLoss != null) return Math.max(0, Number(e.maxLoss));
		return preview?.ok ? Number(preview.value.required) : null;
	}

	function submit() {
		if (!canSubmit || !orders) return;
		const name = t(`strategy.${strategy}`);
		onSubmit({
			orders,
			tag: { combo: nextComboId(session.combos), strategy },
			label: tCount('combo.label', orders.length, { name }),
			risk: risk(),
			unbounded: whatif?.ok === true && whatif.value.expiry != null && whatif.value.expiry.maxLoss == null
		});
	}

	function noBid(l: BuilderLeg): boolean {
		if (l.kind === 'perp' || l.side !== 'sell') return false;
		const q = seriesFor(chain, expiry, l.kind, l.strike ?? NaN);
		return !!q && Number(q.bid) <= 0;
	}

	const legName = (o: { series: number; side: string; qty: string }) => {
		const q = chain.find((x) => x.id === o.series);
		const what = q ? `${q.kind === 'call' ? 'C' : 'P'} ${fmtPrice(q.strike, L)}` : t('lab.perp');
		return `${o.side === 'buy' ? '+' : '−'}${fmtQty(o.qty, L)} ${what}`;
	};
</script>

<section class="panel builder" aria-label={t('builder.title')} data-testid="strategy-builder">
	<h2 class="panel-title">{t('builder.title')} <span class="tag attention">{t('opt.simulated')}</span></h2>
	{#if positionLegs(game).length}
		<button class="small" onclick={() => onLab(positionLegs(game))} data-testid="lab-positions">⚗ {t('builder.labPositions')}</button>
	{/if}
	{#if expiries.length === 0}
		<p class="muted">{t('builder.noExpiry')}</p>
	{:else}
		<div class="row">
			<label>{t('builder.expiry')}
				<select bind:value={expiry} data-testid="builder-expiry">
					{#each expiries as e (e)}<option value={e}>{t('builder.expiryAt', { step: e, n: e - game.step })}</option>{/each}
				</select>
			</label>
			<label>{t('opt.qty')}<input inputmode="decimal" bind:value={nInput} onchange={() => (legs = legs.map((l) => ({ ...l, qty: n })))} data-testid="builder-qty" /></label>
			<label class="check"><input type="checkbox" checked={short} onchange={toggleShort} data-testid="builder-short" /> {t('builder.short')}</label>
		</div>
		<div class="templates" role="group" aria-label={t('builder.templates')}>
			{#each TEMPLATES as id (id)}
				{@const locked = writeLocked(id)}
				<button
					class:primary={template === id}
					disabled={locked}
					aria-describedby={locked ? 'builder-write-locked' : undefined}
					onclick={() => pick(id)}
					data-testid={`template-${id}`}
				>{t(`strategy.${strategyOf(id, short)}`)}</button>
			{/each}
		</div>
		{#if !writeAllowed}<p id="builder-write-locked" class="muted small">{t('builder.writeLocked')}</p>{/if}

		{#if legs.length}
			<table class="legs" data-testid="builder-legs">
				<caption class="muted small">{t(`strategy.${strategy}`)}</caption>
				<tbody>
					{#each legs as l, i (i)}
						<tr data-testid="builder-leg">
							<td>
								<select aria-label={t('lab.kind')} value={l.kind} onchange={(e) => setKind(i, (e.currentTarget as HTMLSelectElement).value as LegKind)}>
									<option value="call">Call</option><option value="put">Put</option><option value="perp">{t('lab.perp')}</option>
								</select>
							</td>
							<td>
								<select aria-label={t('lab.side')} value={l.side} onchange={(e) => edit(i, { side: (e.currentTarget as HTMLSelectElement).value as 'buy' | 'sell' }, true)}>
									<option value="buy">{t('opt.buy')}</option><option value="sell">{t('opt.sell')}</option>
								</select>
							</td>
							<td>
								{#if l.kind !== 'perp'}
									<select aria-label={t('opt.strike')} value={l.strike} onchange={(e) => edit(i, { strike: Number((e.currentTarget as HTMLSelectElement).value) })}>
										{#each strikes as k (k)}<option value={k}>{fmtPrice(String(k), L)}</option>{/each}
									</select>
								{/if}
							</td>
							<td><input aria-label={t('opt.qty')} inputmode="decimal" value={l.qty} onchange={(e) => edit(i, { qty: Math.max(0, parseDecimal((e.currentTarget as HTMLInputElement).value) || 0) })} /></td>
							<td><button aria-label={t('lab.remove')} onclick={() => ((legs = legs.filter((_, j) => j !== i)), (strategy = 'custom'))}>✕</button></td>
						</tr>
						{#if noBid(l)}<tr><td colspan="5" class="attention small" data-testid="builder-no-bid">⚠ <Term id="legging-risk">{t('builder.noBid')}</Term></td></tr>{/if}
					{/each}
				</tbody>
			</table>
		{/if}
		<div class="row">
			<button onclick={addLeg} disabled={legs.filter((l) => l.kind !== 'perp').length >= MAX_OPTION_LEGS} data-testid="builder-add-leg">+ {t('builder.addLeg')}</button>
			{#if held && legs.some((l) => l.kind === 'perp' && l.side === 'buy')}
				<label class="check"><input type="checkbox" bind:checked={useLong} data-testid="builder-use-long" /> {t('builder.useLong', { qty: fmtNum(Number(held.qty), L, 1) })}</label>
			{/if}
		</div>

		{#if problem && legs.length}<p class="warnbox" role="alert" data-testid="builder-problem">{t(`builder.problem.${problem}`)}</p>{/if}

		{#if whatif && !whatif.ok && whatif.error === 'unsupported'}
			<p class="muted small" data-testid="builder-no-analysis">{t('lab.unsupported')}</p>
		{/if}
		{#if preview?.ok && orders && preview.value.legs.length === orders.length}
			{@const p = preview.value}
			<ol class="preview" data-testid="builder-preview" aria-label={t('builder.order')}>
				{#each p.legs as r, i (i)}
					<li class:down={!r.accepted}>
						{legName(orders[i])} @ {fmtPrice(r.quote, L)} —
						{#if r.accepted}✓ {t('builder.accepted')}{:else}<span data-testid="builder-refused">✖ {rejectionText(r.reason)}</span>{/if}
					</li>
				{/each}
			</ol>
			<dl class="kv est" data-testid="builder-totals">
				<dt><Term id="premium">{t(Number(p.netPremium) >= 0 ? 'builder.credit' : 'builder.debit')}</Term></dt><dd>{fmtMoney(String(Math.abs(Number(p.netPremium))), L)}</dd>
				<dt>{t('ticket.estFee')}</dt><dd>{fmtMoney(p.fees, L)}</dd>
				<dt>{t(game.missionRules.spreadMargin ? 'builder.marginSpread' : 'builder.marginPerLeg')}</dt><dd data-testid="builder-margin">{fmtMoney(p.optionMarginAfterFill, L)}</dd>
				<dt>{t('builder.availableAfter')}</dt><dd>{fmtMoney(p.availableAfterFill, L)}</dd>
			</dl>
		{/if}
		{#if whatif?.ok}
			{@const w = whatif.value}
			{@const above = unboundedAbove(w)}
			{#if w.expiry}
				<dl class="kv est" data-testid="builder-analysis">
					<dt><Term id="breakeven">{t('opt.breakeven')}</Term></dt><dd data-testid="builder-breakevens">{w.expiry.breakevens.map((b) => fmtPrice(b, L)).join(' · ') || '—'}</dd>
					<dt>{t('lab.maxProfit')}</dt><dd>{w.expiry.maxProfit != null ? fmtMoney(w.expiry.maxProfit, L) : t('lab.unbounded')}</dd>
					<dt><Term id="defined-risk">{t('lab.maxLoss')}</Term></dt><dd data-testid="builder-max-loss">{w.expiry.maxLoss != null ? fmtMoney(w.expiry.maxLoss, L) : t('lab.unboundedAbove', { price: above ? fmtPrice(above, L) : '—' })}</dd>
				</dl>
			{/if}
			<PayoffChart result={w} testid="builder-payoff" />
			<WhatIfTable result={w} />
			<button onclick={() => orders && onLab(whatifLegs(orders, covers ? held : null))} data-testid="builder-open-lab">⚗ {t('builder.openLab')}</button>
		{/if}
		<p class="muted small">{t('builder.legging')}</p>
		<button class="primary submit" onclick={submit} disabled={!canSubmit} data-testid="builder-submit">{t('builder.submit')}</button>
	{/if}
</section>

<style>
	.builder {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
		overflow: auto;
	}
	.row {
		display: flex;
		flex-wrap: wrap;
		gap: 0.4rem;
		align-items: end;
	}
	label {
		font-size: 0.78rem;
		color: var(--muted);
		display: flex;
		flex-direction: column;
		gap: 0.15rem;
	}
	label.check {
		flex-direction: row;
		align-items: center;
	}
	.templates {
		display: grid;
		grid-template-columns: repeat(2, 1fr);
		gap: 0.25rem;
	}
	.templates button {
		font-size: 0.78rem;
	}
	.legs {
		width: 100%;
		font-size: 0.8rem;
		border-collapse: collapse;
	}
	/* Tight cells so "Vender" and "100,00" fit in the 340 px column at 1024 px. */
	.legs td {
		padding: 0.1rem 0.1rem;
	}
	.legs select {
		padding-left: 0.2rem;
		padding-right: 0.1rem;
	}
	.legs input {
		width: 3.2rem;
	}
	.preview {
		margin: 0;
		padding-left: 1.2rem;
		font-size: 0.8rem;
	}
	.est {
		font-size: 0.82rem;
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
