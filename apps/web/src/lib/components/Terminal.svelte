<script lang="ts">
	import { sound } from '#lib/game/sound.svelte.ts';
	import CoachMarks from './CoachMarks.svelte';
	import { coach } from '#lib/game/coach.svelte.ts';
	import { glossaryUi } from '#lib/game/glossaryUi.svelte.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import type { AdviceView, OrderSideName, OrderView } from '#lib/engine/types.ts';
	import AdvisorPanel from './AdvisorPanel.svelte';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { marginFacts, orderFacts, rejectionText, releverFacts, type RejectionFacts } from '#lib/game/rejections.ts';
	import { AutoPlay, type PauseReason } from '#lib/game/autoplay.svelte.ts';
	import CalibrationDialog from './CalibrationDialog.svelte';
	import Chart from './Chart.svelte';
	import { keepsKey } from '#lib/game/hotkeys.ts';
	import { keymap } from '#lib/game/keymap.svelte.ts';
	import { actionFor, keyName, type ShortcutAction } from '#lib/game/shortcuts.ts';
	import RunJournal from './RunJournal.svelte';
	import { positionPlannedRisk } from '#lib/game/journal.ts';
	import { exitOnValidSide, stopMovedBeyondLiquidation } from './chartDrag.ts';
	import ConfirmDialog from './ConfirmDialog.svelte';
	import { fmtMoney, fmtPrice, fmtQty, num, priceInputText, priceToTick, qtyInputText } from '#lib/game/format.ts';
	import { costsOf, estimatedMarketEntry, fitToMargin, type SizeBy } from '#lib/game/sizing.ts';
	import EventFeed from './EventFeed.svelte';
	import NewsPanel from './NewsPanel.svelte';
	import { newsArrived } from '#lib/game/news.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';
	import HelpOverlay from './HelpOverlay.svelte';
	import OrderTicket, { type OrderDraft, type ProtectTab, type TicketOrderType, type TicketPreview } from './OrderTicket.svelte';
	import TrailNotice from './TrailNotice.svelte';
	import OptionChain from './OptionChain.svelte';
	import OptionTicket, { type OptionDraft } from './OptionTicket.svelte';
	import type { OptionQuoteView } from '#lib/engine/types.ts';
	import OrdersPanel from './OrdersPanel.svelte';
	import PatternSheet from './PatternSheet.svelte';
	import PlanDialog from './PlanDialog.svelte';
	import RightRail from './RightRail.svelte';
	import AssistPanel from './AssistPanel.svelte';
	import TopBar from './TopBar.svelte';
	import StrategyBuilder, { type ComboDraft } from './StrategyBuilder.svelte';
	import ComboNotice from './ComboNotice.svelte';
	import WhatIfLab from './WhatIfLab.svelte';
	import type { WhatIfLegInput } from '#lib/engine/types.ts';
	import { comboContracts, comboStatus, netPremium, submitCombo, workingLegs } from '#lib/game/combos.ts';
	import ComboWorking from './ComboWorking.svelte';
	import PortfolioPanel from './PortfolioPanel.svelte';
	import { PORTFOLIO_REQUEST } from '#lib/game/portfolio.ts';
	import type { WhatIfResult as PortfolioResult } from '#lib/engine/types.ts';
	import type { OrderLeg } from '#lib/game/strategies.ts';
	import { runMaxBars } from '#lib/game/whatifLab.ts';

	let { session }: { session: GameSession } = $props();

	let draft = $state<OrderDraft | null>(null);
	let help = $state(false);
	let journalOpen = $state(false);
	let patterns = $state(false);
	let patternFocus = $state<string | null>(null);

	const NOTABLE: Partial<Record<string, PauseReason>> = {
		fill: 'fill',
		liquidation: 'liquidation',
		margin_call: 'margin_call',
		option_settlement: 'option_settlement'
	};

	// Auto-advance: same advance as Space; waits while any dialog is open, pauses on notable bars.
	const auto = new AutoPlay({
		ready: () =>
			!draft && !optDraft && !comboDraft && !labLegs && !portfolioOpen && !pick && !patterns && !help && !journalOpen && !glossaryUi.open && !session.busy && !session.pendingQuestion && session.state?.status === 'active',
		finished: () => session.state?.status !== 'active',
		advance: () => session.advance(1),
		notable: () => {
			const s = session.state;
			if (!s) return null;
			const hit = s.events.find((e) => e.step === s.step && NOTABLE[e.type]);
			if (hit) return NOTABLE[hit.type] ?? null;
			return newsArrived(s) ? 'news' : null;
		}
	});
	$effect(() => () => auto.stop());
	$effect(() => {
		const r = auto.paused;
		if (r && r !== 'finished') session.notice = { tone: 'warn', text: `⏸ ${t('auto.paused', { reason: t(`auto.reason.${r}`) })}` };
	});

	function openPattern(id: string | null) {
		patternFocus = id;
		patterns = true;
	}
	let side = $state<OrderSideName>('buy');
	let preview = $state<TicketPreview | null>(null);
	// Pending "mark on chart" request from a stop/target field: the chart calls it with a price.
	let pick = $state<((price: number) => void) | null>(null);

	let ticketStop = $state('');
	let ticketTp = $state('');
	let ticketSizeBy = $state<SizeBy>('risk');
	let ticketQty = $state('10');
	let ticketLeverage = $state(1);
	let ticketType = $state<TicketOrderType>('market');
	// The protective section's tab and trailing fields (design D10): the planned-trailing notice fills them.
	let protectTab = $state<ProtectTab>('stop');
	let protTrail = $state('');
	let protTrailPct = $state('');
	let trailMode = $state<'price' | 'pct'>('price');

	// The advisor's stop and size go to the perpetual ticket; nothing is sent until the player submits.
	function onApplyAdvice(a: AdviceView) {
		const c = session.state!.contract;
		side = a.stance === 'short' ? 'sell' : 'buy';
		ticketStop = priceInputText(num(a.stop), num(c.tickSize), i18n.locale);
		ticketSizeBy = 'qty';
		// The read is sized for a market entry at the last price: a stale limit price would change its risk.
		ticketType = 'market';
		// 1% risk at a tight stop can need more than the ticket's leverage: raise it (or cut the size at the
		// cap), never lower what the player typed.
		const s = session.state!;
		const k = costsOf(c, s.liquidity);
		const entry = estimatedMarketEntry(side, lastPrice(s), k);
		const typed = Math.min(Math.max(1, Math.round(ticketLeverage || 1)), c.leverageCap);
		const fit = fitToMargin(num(a.size), entry, num(s.account.available), typed, c.leverageCap, k, num(c.lotSize));
		// Written as the player would type it: "16,6" in PT-BR (before, the dot form "16.6").
		ticketQty = qtyInputText(fit.qty, num(c.lotSize), i18n.locale);
		ticketLeverage = fit.leverage;
		tab = 'perp';
	}

	// Dragging SL?/TP? on the chart edits the ticket fields, snapped to the tick.
	function onDragLevel(which: 'stop' | 'tp', price: number) {
		const tick = num(session.state!.contract.tickSize);
		if (which === 'stop') ticketStop = priceInputText(price, tick, i18n.locale);
		else ticketTp = priceInputText(price, tick, i18n.locale);
	}

	// Dropping a working STOP/TP line moves that order in place (a bracket leg stays in its OCO pair).
	async function onMoveOrder(orderId: number, price: number) {
		const s = session.state;
		const o = s?.orders.find((x) => x.id === orderId);
		if (!s || !o) return;
		const to = priceToTick(price, num(s.contract.tickSize));
		if (to === o.price || Number(to) === Number(o.price)) return;
		if (!exitOnValidSide(o.side, o.type, Number(to), lastPrice(s))) {
			session.notice = { tone: 'error', text: `✖ ${t(o.type === 'stop_market' ? 'chart.dragStopSide' : 'chart.dragTpSide')}` };
			return;
		}
		// Past liquidation: the same confirmation as placing such a stop (the move waits for the answer).
		const liq = s.account.liquidationPrice ? num(s.account.liquidationPrice) : null;
		if (s.missionRules.confirmStopBeyondLiq && stopMovedBeyondLiquidation(o.side, o.type, Number(to), liq)) {
			pendingMove = { o, to };
			return;
		}
		await sendMove(o, to);
	}

	let pendingMove = $state<{ o: OrderView; to: string } | null>(null);
	async function sendMove(o: OrderView, to: string) {
		pendingMove = null;
		const res = await session.moveOrder(o, to);
		if (!res) return;
		if (!res.accepted) return notify(res.reason);
		session.notice = { tone: 'ok', text: t('chart.moved', { id: o.id, price: fmtPrice(to, i18n.locale) }) };
	}

	function onPicked(price: number) {
		pick?.(price);
		pick = null;
	}
	let tab = $state<'perp' | 'options' | 'builder'>('perp');
	let optSeries = $state<number | null>(null);
	let optSide = $state<'buy' | 'sell'>('buy');
	let optDraft = $state<OptionDraft | null>(null);
	const optQuote = $derived<OptionQuoteView | null>(session.state?.options.chain.find((q) => q.id === optSeries) ?? null);
	const optionsOn = $derived(session.state?.options.enabled ?? false);

	function pickOption(q: OptionQuoteView, side: 'buy' | 'sell') {
		optSeries = q.id;
		optSide = side;
		tab = 'options';
	}

	async function sendOption(d: OptionDraft) {
		const res = await session.placeOptionOrder(d.series, d.side, d.type, d.qty, d.premium, undefined, { tif: d.tif, tifBars: d.tifBars });
		if (!res) return;
		if (!res.accepted) return notify(res.reason, d.risk != null ? { need: d.risk, available: num(session.state!.account.available) } : {});
		session.notice = { tone: 'ok', text: `#${res.orderId} ✓ ${sideWord(d.side)} ${d.label} × ${fmtQty(d.qty, i18n.locale)}` };
	}

	function onOptionSubmit(d: OptionDraft) {
		if (d.opensExposure) optDraft = d;
		else void sendOption(d);
	}

	// Plan dialog reuses the perp draft shape; for options only side/qty/risk matter.
	const optPlanDraft = $derived<OrderDraft | null>(
		optDraft
			? {
					side: optDraft.side,
					type: 'market',
					qty: optDraft.qty,
					price: null,
					leverage: 1,
					stop: null,
					takeProfit: null,
					estimate: { notional: 0, margin: 0, fee: 0, liquidation: null, plannedRisk: optDraft.risk, stopBeyondLiquidation: false },
					increases: true,
					required: 0
				}
			: null
	);

	function onOptionPlan(p: PlanEntry) {
		const d = optDraft;
		optDraft = null;
		void session.sequence(async () => {
			await session.recordPlan(p);
			if (d) await sendOption(d);
		});
	}

	// ---- Strategy builder combos (design D3): one plan per leg for the engine, one journal entry ----
	let comboDraft = $state<ComboDraft | null>(null);
	let labLegs = $state<WhatIfLegInput[] | null>(null);
	let dismissed = $state<string[]>([]);
	const comboPlanDraft = $derived<OrderDraft | null>(
		comboDraft ? optPlanDraftOf(comboDraft.orders[0]?.side ?? 'buy', comboDraft.risk, comboContracts(comboDraft.orders)) : null
	);
	const statuses = $derived(session.state ? session.combos.map((r) => comboStatus(session.state!.events, r)) : []);
	const incomplete = $derived(statuses.findLast((st) => st.state === 'incomplete' && !dismissed.includes(st.record.id)) ?? null);
	// Design D2: limit legs still working after their first open (market legs never wait).
	const working = $derived(
		statuses.findLast((st) => st.state === 'working' && workingLegs(st).length > 0 && st.legs.some((l) => l.leg.premium != null) && session.state!.step > (st.record.submissions.at(-1)?.step ?? st.record.step)) ?? null
	);

	function optPlanDraftOf(side: 'buy' | 'sell', risk: number | null, qty = '1'): OrderDraft {
		return {
			side,
			type: 'market',
			qty,
			price: null,
			leverage: 1,
			stop: null,
			takeProfit: null,
			estimate: { notional: 0, margin: 0, fee: 0, liquidation: null, plannedRisk: risk, stopBeyondLiquidation: false },
			increases: true,
			required: 0
		};
	}

	function onComboPlan(p: PlanEntry) {
		const d = comboDraft;
		comboDraft = null;
		if (!d) return;
		void session.sequence(async () => {
			// The engine pairs plans with orders by step, instrument and side: one plan per leg.
			const share = p.plannedRisk != null ? p.plannedRisk / d.orders.length : null;
			for (const [i, o] of d.orders.entries()) {
				const leg: PlanEntry = { ...p, side: o.side, plannedRisk: share, instrument: o.series === 0 ? undefined : d.label };
				await session.recordPlan(leg, i === 0 ? { ...p, instrument: d.label, combo: d.tag.combo } : null);
			}
			const out = await submitCombo(session.comboPort(), d.orders, d.tag, d.tif);
			if (out.ok) session.notice = { tone: 'ok', text: t('combo.sent', { name: d.label }) };
			else session.notice = { tone: 'error', text: `✖ ${t('combo.refused', { n: out.refusedLeg + 1, reason: rejectionText(out.reason) })}` };
		});
	}

	// The journal's combo line: its state and net premium so far (engine fills).
	function comboNote(id: string): string | null {
		const st = statuses.find((x) => x.record.id === id);
		if (!st || !session.state) return null;
		const premium = fmtMoney(netPremium(st, num(session.state.options.multiplier)), i18n.locale, true);
		return `${t(`combo.state.${st.state}`)} · ${t('combo.premium', { amount: premium })}`;
	}

	const labMaxBars = $derived.by(() => {
		const s = session.state;
		if (!s || !labLegs) return 0;
		const steps = labLegs.flatMap((l) => s.options.chain.filter((q) => q.id === l.series).map((q) => q.expiryStep));
		return runMaxBars(steps, s.step);
	});

	async function send(d: OrderDraft) {
		const s = session.state;
		if (!s) return;
		if (d.leverage !== s.leverage) {
			const r = await session.setLeverage(d.leverage);
			if (r && !r.accepted) return notify(r.reason, releverFacts(s, d.leverage));
		}
		const res = await session.placeOrder(d.type, d.side, d.qty, d.price, false, { stop: d.stop, takeProfit: d.takeProfit, tif: d.tif, tifBars: d.tifBars });
		if (!res) return;
		if (!res.accepted) return notify(res.reason, orderFacts(session.state!, d.required, d.leverage));
		session.notice = { tone: 'ok', text: `#${res.orderId} ✓ ${sideWord(d.side)} ${fmtQty(d.qty, i18n.locale)}` };
	}

	const sideWord = (side: 'buy' | 'sell') => t(`side.${side}`);

	function notify(reason: string, facts: RejectionFacts = {}) {
		session.notice = { tone: 'error', text: `✖ ${rejectionText(reason, facts)}` };
	}

	function onSubmit(d: OrderDraft) {
		if (d.increases) draft = d;
		else void send(d);
	}

	function onPlan(p: PlanEntry) {
		const d = draft;
		draft = null;
		void session.sequence(async () => {
			// A planned trailing stop rides with the plan (client data): the notice offers it after the fill.
			await session.recordPlan(d?.plannedTrail ? { ...p, trail: d.plannedTrail } : p);
			if (d) await send(d);
		});
	}

	// Portfolio (design D9): the engine's holdings what-if with its risk block, refreshed on every state
	// change (a reply for an older state is dropped). Only on a build with policy v18's analysis.
	let portfolio = $state<PortfolioResult | null>(null);
	let portfolioOpen = $state(false);
	let portfolioTicket = 0;
	$effect(() => {
		const s = session.state;
		if (!s || !session.orderRulesV18) {
			portfolio = null;
			return;
		}
		void s.digest;
		const mine = ++portfolioTicket;
		void session.runWhatIf({ ...PORTFOLIO_REQUEST, legs: [] }).then((r) => {
			if (mine === portfolioTicket) portfolio = r.ok ? r.value : null;
		});
	});

	// "Hedge delta" (design D9): a normal perpetual ticket at market, through the plan dialog when it adds
	// exposure; the panel already asked for the confirmation.
	function onHedge(o: OrderLeg) {
		portfolioOpen = false;
		const s = session.state!;
		const pos = num(s.position.qty);
		const qty = num(o.qty);
		const increases = pos === 0 || pos > 0 === (o.side === 'buy') || qty > Math.abs(pos);
		onSubmit({ ...optPlanDraftOf(o.side, null, o.qty), leverage: s.leverage, increases });
	}

	// The planned trailing stop's notice filled the protective section: one more click sends it.
	function fillPlannedTrail(mode: 'price' | 'pct', value: string) {
		trailMode = mode;
		if (mode === 'pct') protTrailPct = value;
		else protTrail = value;
		protectTab = 'trail';
		tab = 'perp';
		session.notice = { tone: 'ok', text: t('trail.filled') };
	}

	function typing(e: KeyboardEvent): boolean {
		return keepsKey(e.target as HTMLElement | null, e.key);
	}

	// Shortcuts come from the player's key map (remappable; advance never takes Enter, which submits).
	const actions: Record<ShortcutAction, () => void> = {
		// While auto-advance runs, the advance keys take control back instead of advancing.
		advance: () => (auto.running ? auto.stop() : session.advance(1)),
		advance5: () => (auto.running ? auto.stop() : session.advance(5)),
		auto: () => auto.toggle(),
		mute: () => sound.toggleMute(),
		buy: () => (side = 'buy'),
		sell: () => (side = 'sell'),
		close: () => void session.closePosition(),
		help: () => (help = !help),
		patterns: () => openPattern(null),
		options: () => {
			if (optionsOn) tab = tab === 'options' ? 'perp' : 'options';
		},
		journal: () => (journalOpen = true)
	};

	// The pattern sheet, the help overlay and the journal are modal: only their own key and Esc act,
	// so the advance key cannot move the market behind them.
	function modalKey(e: KeyboardEvent, action: ShortcutAction | null): boolean {
		const closes = (own: ShortcutAction) => e.key === 'Escape' || action === own;
		if (patterns) patterns = !closes('patterns');
		else if (help) help = !closes('help');
		else if (journalOpen) journalOpen = !closes('journal');
		else return false;
		return true;
	}

	function onKey(e: KeyboardEvent) {
		if (keymap.capturing || draft || optDraft || comboDraft || labLegs || portfolioOpen || session.pendingQuestion || pick || glossaryUi.open) return;
		// The coach mark's buttons keep Space and Enter (the market never advances from the callout).
		if ((e.target as HTMLElement | null)?.closest?.('[data-own-keys]')) return;
		const action = actionFor(keymap.map, keyName(e));
		if (modalKey(e, action)) return;
		if (typing(e) || e.metaKey || e.ctrlKey || e.altKey || !action) return;
		e.preventDefault();
		actions[action]();
	}
</script>

<svelte:window onkeydown={onKey} />

{#if session.state}
	<div class="terminal">
		<TopBar {session} {auto} onHelp={() => (help = true)} onPatterns={() => openPattern(null)} />
		<div class="main">
			<section class="panel chart" data-coach="chart"><Chart
					game={session.state}
					preview={tab === 'perp' ? preview : null}
					picking={pick != null}
					{onPicked}
					onCancelPick={() => (pick = null)}
					{onDragLevel}
					{onMoveOrder}
					onPattern={openPattern}
				/></section>
			<RightRail
				game={session.state}
				risk={portfolio?.risk ?? null}
				onPortfolio={session.orderRulesV18 ? () => (portfolioOpen = true) : undefined}
				plannedRisk={Number(session.state.position.qty) !== 0 ? positionPlannedRisk(session.journal) : null}
				onMargin={async (amount) => {
					const before = session.state!;
					const r = await session.adjustMargin(amount);
					if (r && !r.accepted) notify(r.reason, marginFacts(before, Number(amount)));
				}}
			/>
		</div>
		<div class="bottom">
			<div class="stack">
				{#if optionsOn}
					<div class="tabs" role="tablist">
						<button role="tab" aria-selected={tab === 'perp'} class:primary={tab === 'perp'} onclick={() => (tab = 'perp')}>{t('opt.tabPerp')}</button>
						<button role="tab" aria-selected={tab === 'options'} class:primary={tab === 'options'} onclick={() => (tab = 'options')}>{t('opt.tabOptions')}</button>
						<button role="tab" aria-selected={tab === 'builder'} class:primary={tab === 'builder'} onclick={() => (tab = 'builder')} data-testid="tab-builder">{t('builder.tab')}</button>
					</div>
				{/if}
				{#if tab === 'builder' && optionsOn}
					<StrategyBuilder {session} onSubmit={(d) => (comboDraft = d)} onLab={(legs) => (labLegs = legs)} />
				{:else if tab === 'options' && optionsOn}
					<OptionTicket game={session.state} quote={optQuote} bind:side={optSide} busy={session.busy} tifAllowed={session.orderRulesV18} onSubmit={onOptionSubmit} />
				{:else}
					<OrderTicket {session} bind:side bind:preview bind:stopInput={ticketStop} bind:tpInput={ticketTp} bind:sizeBy={ticketSizeBy} bind:qtyInput={ticketQty} bind:leverage={ticketLeverage} bind:orderType={ticketType} bind:protectTab bind:protTrail bind:protTrailPct bind:trailMode {onSubmit} onPick={(set) => (pick = set)} />
				{/if}
			</div>
			<div class="stack">
				{#if optionsOn}
					<OptionChain game={session.state} selected={optSeries} onPick={pickOption} />
				{/if}
				{#if incomplete}
					<ComboNotice {session} status={incomplete} onDone={() => (dismissed = [...dismissed, incomplete.record.id])} />
				{/if}
				{#if working}
					<ComboWorking {session} status={working} />
				{/if}
				<TrailNotice game={session.state} journal={session.journal} onFill={fillPlannedTrail} />
				<OrdersPanel {session} />
				{#if session.notice}
					<div class="notice {session.notice.tone}" role="status">{session.notice.text}</div>
				{/if}
			</div>
			<div class="stack side">
				<!-- Practice packs and assisted attempts start without the advisor, so it is not offered. -->
				{#if !session.state.knownScenario && !session.runAssisted}
					<AdvisorPanel {session} onApply={onApplyAdvice} />
				{/if}
				<NewsPanel news={session.state.news} calendar={session.state.calendar} upcoming={session.state.upcoming ?? []} step={session.state.step} />
				<EventFeed events={session.state.events} names={session.names} />
				<AssistPanel {session} />
			</div>
		</div>
	</div>
{/if}

{#if session.pendingQuestion && !draft}
	<CalibrationDialog {session} />
{/if}
{#if optDraft && optPlanDraft && session.state}
	<PlanDialog draft={optPlanDraft} step={session.state.step} optionLabel={optDraft.label} onConfirm={onOptionPlan} onCancel={() => (optDraft = null)} />
{/if}
{#if draft && session.state}
	<PlanDialog {draft} step={session.state.step} onConfirm={onPlan} onCancel={() => (draft = null)} />
{/if}
{#if comboDraft && comboPlanDraft && session.state}
	<PlanDialog
		draft={comboPlanDraft}
		step={session.state.step}
		optionLabel={comboDraft.label}
		riskUnbounded={comboDraft.unbounded}
		onConfirm={onComboPlan}
		onCancel={() => (comboDraft = null)}
	/>
{/if}
{#if labLegs && session.state}
	<div class="modal-backdrop" role="presentation">
		<div class="modal lab-modal" role="dialog" aria-modal="true" aria-labelledby="lab-modal-title">
			<h2 id="lab-modal-title">{t('lab.title')}</h2>
			<WhatIfLab {session} mode="terminal" runLegs={labLegs} forward={lastPrice(session.state)} maxBars={labMaxBars} />
			<button class="primary" onclick={() => (labLegs = null)} data-testid="lab-close">{t('lab.close')}</button>
		</div>
	</div>
{/if}
{#if portfolioOpen && session.state}
	<div class="modal-backdrop" role="presentation">
		<div class="modal lab-modal" role="dialog" aria-modal="true" aria-labelledby="portfolio-title">
			<PortfolioPanel {session} result={portfolio} {onHedge} />
			<button class="primary" onclick={() => (portfolioOpen = false)} data-testid="portfolio-close">{t('portfolio.close')}</button>
		</div>
	</div>
{/if}
{#if pendingMove && session.state}
	{@const pm = pendingMove}
	<ConfirmDialog
		title={t('chart.moveBeyondTitle')}
		message={t('chart.moveBeyond', {
			id: pm.o.id,
			price: fmtPrice(pm.to, i18n.locale),
			liq: fmtPrice(session.state.account.liquidationPrice, i18n.locale)
		})}
		confirmLabel={t('chart.moveBeyondYes')}
		cancelLabel={t('chart.moveBeyondNo')}
		onConfirm={() => void sendMove(pm.o, pm.to)}
		onCancel={() => (pendingMove = null)}
	/>
{/if}
{#if help}
	<HelpOverlay
		onClose={() => (help = false)}
		onTour={() => {
			help = false;
			coach.start(session.currentRunId);
		}}
	/>
{/if}
<CoachMarks {session} place="terminal" />
{#if journalOpen}
	<RunJournal entries={session.journal} onClose={() => (journalOpen = false)} {comboNote} />
{/if}
{#if patterns}
	<PatternSheet focus={patternFocus} onClose={() => (patterns = false)} />
{/if}

<style>
	.terminal {
		display: grid;
		grid-template-rows: auto minmax(300px, 1fr) minmax(360px, 44vh);
		gap: 0.5rem;
		height: 100vh;
		padding: 0.5rem;
	}
	.main {
		display: grid;
		grid-template-columns: minmax(0, 1fr) 300px;
		gap: 0.5rem;
		min-height: 0;
	}
	.chart {
		min-height: 0;
	}
	.bottom {
		display: grid;
		grid-template-columns: 360px minmax(0, 1fr) minmax(0, 1.2fr);
		gap: 0.5rem;
		min-height: 0;
	}
	.bottom > :global(*) {
		min-height: 0;
	}
	.stack {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
		min-height: 0;
	}
	/* Advisor, news and feed: the feed takes what is left; when that is not enough the column scrolls. */
	.stack.side {
		overflow: auto;
	}
	.stack.side > :global(.feed) {
		min-height: 6rem;
	}
	.tabs {
		display: grid;
		grid-template-columns: 1fr 1fr 1fr;
		gap: 0.3rem;
	}
	.lab-modal {
		max-width: 860px;
		width: 95vw;
		max-height: 92vh;
		overflow: auto;
	}
	.notice {
		padding: 0.4rem 0.6rem;
		border: 1px solid var(--line);
		font-size: 0.85rem;
	}
	.notice.ok {
		border-color: var(--accent);
		color: var(--accent);
	}
	.notice.error {
		border-color: var(--danger);
		color: var(--danger);
	}
	.notice.warn {
		border-color: var(--attention);
		color: var(--attention);
	}
	/* 1024-1279: chart and rail stay side by side (spec: >= 1024 landscape); the lower area folds
	   into ticket | chain + orders, with news and the feed below, and the page scrolls. */
	@media (max-width: 1279px) {
		.terminal {
			height: auto;
			grid-template-rows: auto minmax(420px, 62vh) auto;
		}
		.bottom {
			grid-template-columns: 340px minmax(0, 1fr);
		}
		.bottom > .stack:last-child {
			grid-column: 1 / -1;
		}
	}
	/* Below 1024 everything stacks at its natural height (the chart keeps a fixed one). */
	@media (max-width: 1023px) {
		.terminal {
			grid-template-rows: auto auto auto;
		}
		.main,
		.bottom {
			grid-template-columns: minmax(0, 1fr);
		}
		.chart {
			height: 420px;
		}
	}
</style>
