<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import type { OrderSideName } from '#lib/engine/types.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import CalibrationDialog from './CalibrationDialog.svelte';
	import Chart from './Chart.svelte';
	import EventFeed from './EventFeed.svelte';
	import HelpOverlay from './HelpOverlay.svelte';
	import OrderTicket, { type OrderDraft } from './OrderTicket.svelte';
	import OrdersPanel from './OrdersPanel.svelte';
	import PlanDialog from './PlanDialog.svelte';
	import RightRail from './RightRail.svelte';
	import TopBar from './TopBar.svelte';

	let { session }: { session: GameSession } = $props();

	let draft = $state<OrderDraft | null>(null);
	let help = $state(false);
	let side = $state<OrderSideName>('buy');
	let lastPlannedRisk = $state<number | null>(null);

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
		session.notice = { tone: 'error', text: `✖ ${reason}` };
	}

	function onSubmit(d: OrderDraft) {
		if (d.increases) draft = d;
		else void send(d);
	}

	function onPlan(p: PlanEntry) {
		const d = draft;
		draft = null;
		session.recordPlan(p);
		if (d) void send(d);
	}

	function typing(e: KeyboardEvent): boolean {
		const el = e.target as HTMLElement | null;
		return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT');
	}

	// Advance (Space) never shares a key with submission (Enter inside the ticket).
	function onKey(e: KeyboardEvent) {
		if (draft || session.pendingQuestion) return;
		if (e.key === 'Escape') help = false;
		if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
		const actions: Record<string, () => void> = {
			' ': () => session.advance(e.shiftKey ? 5 : 1),
			b: () => (side = 'buy'),
			s: () => (side = 'sell'),
			x: () => void session.closePosition(),
			'?': () => (help = !help)
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
		<TopBar {session} onHelp={() => (help = true)} />
		<div class="main">
			<section class="panel chart"><Chart game={session.state} /></section>
			<RightRail state={session.state} plannedRisk={Number(session.state.position.qty) !== 0 ? lastPlannedRisk : null} />
		</div>
		<div class="bottom">
			<OrderTicket {session} bind:side {onSubmit} />
			<div class="stack">
				<OrdersPanel {session} />
				{#if session.notice}
					<div class="notice {session.notice.tone}" role="status">{session.notice.text}</div>
				{/if}
			</div>
			<EventFeed events={session.state.events} />
		</div>
	</div>
{/if}

{#if session.pendingQuestion && !draft}
	<CalibrationDialog {session} />
{/if}
{#if draft && session.state}
	<PlanDialog {draft} step={session.state.step} onConfirm={onPlan} onCancel={() => (draft = null)} />
{/if}
{#if help}
	<HelpOverlay onClose={() => (help = false)} />
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
