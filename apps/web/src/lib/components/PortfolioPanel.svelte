<script lang="ts">
	// Portfolio (add-practice-and-advanced-strategies D9, W1-W8): every position with the engine's value,
	// P&L and per-unit Greeks; the book's totals from the engine's risk block (delta in units and money,
	// gamma of a 1% move, vega per point, theta per bar, the next funding payment, each in bp of equity);
	// the mission's risk limits with warnings that never block; and "Hedge delta", a normal perpetual
	// ticket (plan dialog and a confirmation), never sent on its own.
	import type { RiskLimitKind, WhatIfResult } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtMoney, fmtNum, fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { declaredLimits, deltaAfterHedge, exceededLimits, hedgeTicket, portfolioRows } from '#lib/game/portfolio.ts';
	import type { OrderLeg } from '#lib/game/strategies.ts';
	import ConfirmDialog from './ConfirmDialog.svelte';
	import HeatmapPanel from './HeatmapPanel.svelte';
	import Term from './Term.svelte';

	let { session, result, onHedge }: { session: GameSession; result: WhatIfResult | null; onHedge: (o: OrderLeg) => void } = $props();

	const L = $derived(i18n.locale);
	const game = $derived(session.state!);
	const risk = $derived(result?.risk ?? null);
	const rows = $derived(result ? portfolioRows(result) : []);
	const limits = $derived(declaredLimits(game));
	const warnings = $derived(exceededLimits(risk));
	const hedge = $derived(hedgeTicket(game, risk));
	let confirming = $state(false);

	function name(r: (typeof rows)[number]): string {
		if (r.leg.kind === 'perp') return t('lab.perp');
		return `${r.leg.kind === 'call' ? 'C' : 'P'} ${fmtPrice(r.leg.strike, L)} @${r.leg.expiryStep}`;
	}
	const bp = (v: number) => t('portfolio.bp', { bp: fmtNum(v, L, 0) });
	const kindName = (k: RiskLimitKind) => t(`portfolio.limit.${k}`);
</script>

<section class="portfolio" aria-labelledby="portfolio-title" data-testid="portfolio">
	<h3 id="portfolio-title"><Term id="portfolio-greeks">{t('portfolio.title')}</Term></h3>
	{#if !result}
		<p class="muted">{t('lab.unsupportedV18')}</p>
	{:else}
		{#if rows.length === 0}
			<p class="muted" data-testid="portfolio-empty">{t('portfolio.empty')}</p>
		{:else}
			<table class="rows" data-testid="portfolio-rows">
				<thead>
					<tr><th scope="col">{t('portfolio.position')}</th><th scope="col" class="num">{t('portfolio.qty')}</th><th scope="col" class="num">{t('portfolio.value')}</th><th scope="col" class="num">{t('portfolio.pnl')}</th><th scope="col" class="num">Δ</th><th scope="col" class="num">Γ</th><th scope="col" class="num">Θ</th><th scope="col" class="num">ν</th></tr>
				</thead>
				<tbody>
					{#each rows as r, i (i)}
						<tr>
							<th scope="row">{name(r)}</th>
							<td class="num">{fmtQty(String(r.qty), L)}</td>
							<td class="num">{fmtMoney(r.leg.positionValue, L)}</td>
							<td class="num {Number(r.leg.pnl) >= 0 ? 'up' : 'down'}">{fmtMoney(r.leg.pnl, L, true)}</td>
							<td class="num">{fmtNum(Number(r.leg.delta), L, 3)}</td>
							<td class="num">{fmtNum(Number(r.leg.gamma), L, 4)}</td>
							<td class="num">{fmtNum(Number(r.leg.theta), L, 3)}</td>
							<td class="num">{fmtNum(Number(r.leg.vega), L, 3)}</td>
						</tr>
					{/each}
				</tbody>
			</table>
			<p class="muted small">{t('portfolio.perUnit')}</p>
		{/if}
		{#if risk}
			<dl class="kv totals" data-testid="portfolio-totals">
				<dt><Term id="delta">{t('portfolio.deltaUnits')}</Term></dt><dd data-testid="portfolio-delta">{fmtNum(Number(risk.deltaUnits), L, 4)}</dd>
				<dt>{t('portfolio.deltaNotional')}</dt><dd>{fmtMoney(risk.deltaNotional, L, true)} · {bp(risk.deltaBp)}</dd>
				<dt><Term id="gamma">{t('portfolio.gamma1pct')}</Term></dt><dd>{fmtMoney(risk.gamma1pct, L)} · {bp(risk.gammaBp)}</dd>
				<dt><Term id="vega">{t('portfolio.vegaPoint')}</Term></dt><dd>{fmtMoney(risk.vegaPoint, L, true)} · {bp(risk.vegaBp)}</dd>
				<dt><Term id="theta">{t('portfolio.thetaBar')}</Term></dt><dd>{fmtMoney(risk.thetaBar, L, true)} · {bp(risk.thetaBp)}</dd>
				<dt><Term id="funding-rate">{t('portfolio.fundingNext')}</Term></dt><dd>{fmtMoney(risk.fundingNext, L, true)} · {bp(risk.fundingBp)}</dd>
			</dl>
		{/if}
		<h4><Term id="risk-limit">{t('portfolio.limits')}</Term></h4>
		{#if limits.length === 0}
			<p class="muted small" data-testid="portfolio-no-limits">{t('portfolio.noLimits')}</p>
		{:else}
			<ul class="limits" data-testid="portfolio-limits">
				{#each risk?.limits ?? [] as l (l.kind)}
					<li class:warn={l.exceeded}>{t('portfolio.limitRow', { kind: kindName(l.kind), value: fmtNum(l.valueBp, L, 0), limit: fmtNum(l.limitBp, L, 0) })}</li>
				{/each}
			</ul>
			{#each warnings as w (w.kind)}
				<p class="warnbox" role="alert" data-testid="portfolio-warning">{t('portfolio.exceeded', { kind: kindName(w.kind), value: fmtNum(w.valueBp, L, 0), limit: fmtNum(w.limitBp, L, 0) })}</p>
			{/each}
		{/if}
		<div class="hedge">
			<button disabled={!hedge || session.busy} onclick={() => (confirming = true)} data-testid="portfolio-hedge">{t('portfolio.hedge')}</button>
			<span class="muted small" data-testid="portfolio-hedge-note">
				<Term id="delta-hedge">{t('portfolio.hedge')}</Term>:
				{hedge ? t('portfolio.hedgeNote', { qty: fmtQty(hedge.qty, L), side: t(`side.${hedge.side}`) }) : t('portfolio.hedgeNone')}
			</span>
		</div>
		{#if rows.length}<HeatmapPanel {session} legs={[]} holdings testid="portfolio-heatmap" />{/if}
	{/if}
</section>

{#if confirming && hedge && risk}
	<ConfirmDialog
		title={t('portfolio.hedgeTitle')}
		message={t('portfolio.hedgeConfirm', { side: t(`side.${hedge.side}`), qty: fmtQty(hedge.qty, L), after: fmtNum(deltaAfterHedge(game, risk, hedge), L, 4) })}
		confirmLabel={t('portfolio.hedgeYes')}
		cancelLabel={t('orders.linkNo')}
		onConfirm={() => {
			confirming = false;
			onHedge(hedge);
		}}
		onCancel={() => (confirming = false)}
	/>
{/if}

<style>
	.portfolio {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	h3,
	h4 {
		margin: 0;
		font-size: 0.9rem;
	}
	.rows {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.78rem;
	}
	.rows th,
	.rows td {
		padding: 0.1rem 0.25rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.rows td.num,
	.rows th.num {
		text-align: right;
	}
	.totals {
		font-size: 0.82rem;
	}
	.limits {
		margin: 0;
		padding-left: 1rem;
		font-size: 0.8rem;
	}
	.limits li.warn {
		color: var(--danger);
	}
	.warnbox {
		border: 1px solid var(--danger);
		color: var(--danger);
		padding: 0.3rem 0.5rem;
		font-size: 0.8rem;
		margin: 0;
	}
	.hedge {
		display: flex;
		gap: 0.4rem;
		align-items: center;
		flex-wrap: wrap;
	}
	.small {
		font-size: 0.75rem;
	}
</style>
