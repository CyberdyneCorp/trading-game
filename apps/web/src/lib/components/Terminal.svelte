<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import type { OrderSideName } from '#lib/engine/types.ts';
	import { reasonText, t } from '#lib/game/i18n.svelte.ts';
	import { AutoPlay, type PauseReason } from '#lib/game/autoplay.svelte.ts';
	import CalibrationDialog from './CalibrationDialog.svelte';
	import Chart from './Chart.svelte';
	import EventFeed from './EventFeed.svelte';
	import NewsPanel from './NewsPanel.svelte';
	import HelpOverlay from './HelpOverlay.svelte';
	import OrderTicket, { type OrderDraft, type TicketPreview } from './OrderTicket.svelte';
	import OptionChain from './OptionChain.svelte';
	import OptionTicket, { type OptionDraft } from './OptionTicket.svelte';
	import type { OptionQuoteView } from '#lib/engine/types.ts';
	import OrdersPanel from './OrdersPanel.svelte';
	import PatternSheet from './PatternSheet.svelte';
	import PlanDialog from './PlanDialog.svelte';
	import RightRail from './RightRail.svelte';
	import TopBar from './TopBar.svelte';

	let { session }: { session: GameSession } = $props();

	let draft = $state<OrderDraft | null>(null);
	let help = $state(false);
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
			!draft && !optDraft && !pick && !patterns && !help && !session.busy && !session.pendingQuestion && session.state?.status === 'active',
		finished: () => session.state?.status !== 'active',
		advance: () => session.advance(1),
		notable: () => {
			const s = session.state;
			if (!s) return null;
			const hit = s.events.find((e) => e.step === s.step && NOTABLE[e.type]);
			if (hit) return NOTABLE[hit.type] ?? null;
			return s.news.some((n) => n.step === s.step) ? 'news' : null;
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
	let lastPlannedRisk = $state<number | null>(null);
	let preview = $state<TicketPreview | null>(null);
	// Pending "mark on chart" request from a stop/target field: the chart calls it with a price.
	let pick = $state<((price: number) => void) | null>(null);

	function onPicked(price: number) {
		pick?.(price);
		pick = null;
	}
	let tab = $state<'perp' | 'options'>('perp');
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
		const res = await session.placeOptionOrder(d.series, d.side, d.type, d.qty, d.premium);
		if (!res) return;
		if (!res.accepted) return notify(res.reason);
		session.notice = { tone: 'ok', text: `#${res.orderId} ✓ ${d.side} ${d.label} × ${d.qty}` };
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
					increases: true
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

	async function send(d: OrderDraft) {
		const s = session.state;
		if (!s) return;
		if (d.leverage !== s.leverage) {
			const r = await session.setLeverage(d.leverage);
			if (r && !r.accepted) return notify(r.reason);
		}
		const res = await session.placeOrder(d.type, d.side, d.qty, d.price, false, { stop: d.stop, takeProfit: d.takeProfit });
		if (!res) return;
		if (!res.accepted) return notify(res.reason);
		lastPlannedRisk = d.estimate.plannedRisk;
		session.notice = { tone: 'ok', text: `#${res.orderId} ✓ ${d.side} ${d.qty}` };
	}

	function notify(reason: string) {
		session.notice = { tone: 'error', text: `✖ ${reasonText(reason)}` };
	}

	function onSubmit(d: OrderDraft) {
		if (d.increases) draft = d;
		else void send(d);
	}

	function onPlan(p: PlanEntry) {
		const d = draft;
		draft = null;
		void session.sequence(async () => {
			await session.recordPlan(p);
			if (d) await send(d);
		});
	}

	function typing(e: KeyboardEvent): boolean {
		const el = e.target as HTMLElement | null;
		return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT');
	}

	// Advance (Space) never shares a key with submission (Enter inside the ticket).
	function onKey(e: KeyboardEvent) {
		if (draft || optDraft || session.pendingQuestion || pick) return;
		if (patterns) {
			// The sheet is modal: only P / Esc (close) reach the game, so Space cannot advance behind it.
			if (e.key === 'Escape' || e.key.toLowerCase() === 'p') patterns = false;
			return;
		}
		if (help) {
			// Help is modal too: Space must not advance the market behind it.
			if (e.key === 'Escape' || e.key === '?') help = false;
			return;
		}
		if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
		const actions: Record<string, () => void> = {
			// While auto-advance runs, Space takes control back instead of advancing.
			' ': () => (auto.running ? auto.stop() : session.advance(e.shiftKey ? 5 : 1)),
			a: () => auto.toggle(),
			b: () => (side = 'buy'),
			s: () => (side = 'sell'),
			x: () => void session.closePosition(),
			'?': () => (help = !help),
			p: () => openPattern(null)
		};
		const fn = actions[e.key.toLowerCase()];
		if (fn) {
			e.preventDefault();
			fn();
		}
	}
</script>

<svelte:window onkeydown={onKey} />

{#if session.state}
	<div class="terminal">
		<TopBar {session} {auto} onHelp={() => (help = true)} onPatterns={() => openPattern(null)} />
		<div class="main">
			<section class="panel chart"><Chart
					game={session.state}
					preview={tab === 'perp' ? preview : null}
					picking={pick != null}
					{onPicked}
					onCancelPick={() => (pick = null)}
					onPattern={openPattern}
				/></section>
			<RightRail
				game={session.state}
				plannedRisk={Number(session.state.position.qty) !== 0 ? lastPlannedRisk : null}
				onMargin={async (amount) => {
					const r = await session.adjustMargin(amount);
					if (r && !r.accepted) notify(r.reason);
				}}
			/>
		</div>
		<div class="bottom">
			<div class="stack">
				{#if optionsOn}
					<div class="tabs" role="tablist">
						<button role="tab" aria-selected={tab === 'perp'} class:primary={tab === 'perp'} onclick={() => (tab = 'perp')}>{t('opt.tabPerp')}</button>
						<button role="tab" aria-selected={tab === 'options'} class:primary={tab === 'options'} onclick={() => (tab = 'options')}>{t('opt.tabOptions')}</button>
					</div>
				{/if}
				{#if tab === 'options' && optionsOn}
					<OptionTicket game={session.state} quote={optQuote} bind:side={optSide} busy={session.busy} onSubmit={onOptionSubmit} />
				{:else}
					<OrderTicket {session} bind:side bind:preview {onSubmit} onPick={(set) => (pick = set)} />
				{/if}
			</div>
			<div class="stack">
				{#if optionsOn}
					<OptionChain game={session.state} selected={optSeries} onPick={pickOption} />
				{/if}
				<OrdersPanel {session} />
				{#if session.notice}
					<div class="notice {session.notice.tone}" role="status">{session.notice.text}</div>
				{/if}
			</div>
			<div class="stack">
				<NewsPanel news={session.state.news} />
				<EventFeed events={session.state.events} />
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
{#if help}
	<HelpOverlay onClose={() => (help = false)} />
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
	.tabs {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 0.3rem;
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
	@media (max-width: 1100px) {
		.terminal {
			height: auto;
			grid-template-rows: auto 420px auto;
		}
		.main,
		.bottom {
			grid-template-columns: 1fr;
		}
	}
</style>
