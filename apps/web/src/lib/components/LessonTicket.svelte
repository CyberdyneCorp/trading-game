<script lang="ts">
	// Lesson ticket sandbox (design D2 `ticket`): a perpetual ticket that sends nothing. The step fixes
	// the market, equity and entry; the fields it lists in `edit` can be changed. The outputs come from
	// the terminal's display mirrors with costs left out (content/ticketSandbox.ts).
	import type { TicketWidget } from '#lib/content/academy.ts';
	import { sandboxOutputs, type SandboxInputs } from '#lib/content/ticketSandbox.ts';
	import { fmtMoney, fmtNum, fmtPct, parseDecimal } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import Term from './Term.svelte';

	let { widget, inputs = $bindable() }: { widget: TicketWidget; inputs: SandboxInputs } = $props();

	const L = $derived(i18n.locale);
	const out = $derived(sandboxOutputs(widget, inputs));
	const can = (f: string) => (widget.edit as string[]).includes(f);
	const price = (v: number | null) => (v == null ? '—' : fmtNum(v, L, 2));

	function setNumber(field: 'qty' | 'stop' | 'target', raw: string) {
		const v = parseDecimal(raw);
		inputs = { ...inputs, [field]: Number.isFinite(v) ? v : field === 'qty' ? 0 : null };
	}
</script>

<figure class="sandbox" data-testid="lesson-ticket">
	<figcaption>
		<span>{t('academy.market', { market: widget.market, interval: t('academy.sandbox') })}</span>
		<span class="tag attention">{t('academy.illustrative')}</span>
	</figcaption>
	<div class="grid">
		<span class="fixed">{t('academy.ticket.equity')}: <strong>{fmtMoney(widget.equity, L)}</strong></span>
		<span class="fixed">{t('academy.ticket.entry')}: <strong>{price(widget.entry)}</strong></span>
		{#if can('side')}
			<div class="sides" role="radiogroup" aria-label={t('academy.ticket.side')}>
				<button role="radio" aria-checked={inputs.side === 'buy'} class:active={inputs.side === 'buy'} onclick={() => (inputs = { ...inputs, side: 'buy' })}>▲ {t('ticket.buy')}</button>
				<button role="radio" aria-checked={inputs.side === 'sell'} class:active={inputs.side === 'sell'} onclick={() => (inputs = { ...inputs, side: 'sell' })}>▼ {t('ticket.sell')}</button>
			</div>
		{:else}
			<span class="fixed">{t('academy.ticket.side')}: <strong>{inputs.side === 'buy' ? t('ticket.buy') : t('ticket.sell')}</strong></span>
		{/if}
		<label>
			{t('ticket.qty')}
			<input inputmode="decimal" value={String(inputs.qty)} disabled={!can('qty')} oninput={(e) => setNumber('qty', e.currentTarget.value)} data-testid="sandbox-qty" />
		</label>
		<label>
			<Term id="leverage">{t('ticket.leverage')}</Term> (≤ {widget.maxLeverage}x)
			<input
				type="number"
				min="1"
				max={widget.maxLeverage}
				step="1"
				value={inputs.leverage}
				disabled={!can('leverage')}
				oninput={(e) => (inputs = { ...inputs, leverage: Number(e.currentTarget.value) || 1 })}
				data-testid="sandbox-leverage"
			/>
		</label>
		<label>
			<Term id="stop-loss">{t('ticket.stopLoss')}</Term>
			<input inputmode="decimal" value={inputs.stop == null ? '' : String(inputs.stop)} disabled={!can('stop')} oninput={(e) => setNumber('stop', e.currentTarget.value)} />
		</label>
		{#if widget.target != null || can('target')}
			<label>
				<Term id="take-profit">{t('ticket.takeProfit')}</Term>
				<input inputmode="decimal" value={inputs.target == null ? '' : String(inputs.target)} disabled={!can('target')} oninput={(e) => setNumber('target', e.currentTarget.value)} />
			</label>
		{/if}
	</div>
	<dl class="kv out" aria-live="polite">
		<dt><Term id="notional">{t('ticket.estNotional')}</Term></dt><dd>{fmtMoney(out.notional, L)}</dd>
		<dt><Term id="initial-margin">{t('ticket.estMargin')}</Term></dt><dd data-testid="sandbox-margin">{fmtMoney(out.margin, L)}</dd>
		<dt><Term id="liquidation-price">{t('ticket.estLiq')}</Term></dt><dd class="down" data-testid="sandbox-liq">{price(out.liquidation)}</dd>
		<dt><Term id="risk-budget">{t('ticket.estRisk')}</Term></dt>
		<dd data-testid="sandbox-risk">{out.riskAtStop == null ? '—' : `${fmtMoney(out.riskAtStop, L)} (${fmtPct((out.riskPct ?? 0) / 100, L)})`}</dd>
		{#if out.rTarget != null}<dt><Term id="r-multiple">{t('academy.ticket.r')}</Term></dt><dd>{fmtNum(out.rTarget, L, 2)}R</dd>{/if}
	</dl>
	{#if out.stopBeyondLiquidation}<p class="down small" role="status">⚠ {t('academy.ticket.beyondLiq')}</p>{/if}
	<p class="muted small">{t('academy.ticket.noCosts')}</p>
</figure>

<style>
	.sandbox {
		margin: 0.6rem 0;
		border: 1px solid var(--line);
		padding: 0.5rem;
		background: var(--bg);
	}
	figcaption {
		display: flex;
		justify-content: space-between;
		font-size: 0.75rem;
		color: var(--muted);
	}
	.grid {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
		gap: 0.4rem;
		margin: 0.4rem 0;
		align-items: end;
	}
	.grid label {
		display: flex;
		flex-direction: column;
		gap: 0.15rem;
		font-size: 0.85rem;
	}
	.fixed {
		font-size: 0.85rem;
	}
	.sides {
		display: flex;
		gap: 0.3rem;
	}
	.sides .active {
		border-color: var(--accent);
		color: var(--accent);
	}
	.out {
		grid-template-columns: auto 1fr;
	}
	.small {
		font-size: 0.8rem;
		margin: 0.2rem 0 0;
	}
</style>
