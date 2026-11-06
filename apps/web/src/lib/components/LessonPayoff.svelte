<script lang="ts">
	// Payoff at expiry of a lesson position (design D2 `payoff`): one to four option legs and an
	// optional perpetual leg, with strikes, breakevens and the maximum profit and loss (content/payoff.ts,
	// exact on the piecewise-linear expiry payoff). A step that asks for those numbers hides them.
	import type { PayoffWidget } from '#lib/content/academy.ts';
	import { breakevens, maxLoss, maxProfit, payoffCurve } from '#lib/content/payoff.ts';
	import { fmtNum } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import Term from './Term.svelte';

	let { widget }: { widget: PayoffWidget } = $props();

	const W = 480;
	const H = 200;
	const PAD = 24;
	const L = $derived(i18n.locale);
	const curve = $derived(payoffCurve(widget.legs, widget.lo, widget.hi, 80));
	const top = $derived(Math.max(...curve.map((p) => p.pnl), 0));
	const bottom = $derived(Math.min(...curve.map((p) => p.pnl), 0));
	const x = (s: number) => PAD + ((s - widget.lo) / (widget.hi - widget.lo)) * (W - 2 * PAD);
	const y = (v: number) => PAD / 2 + ((top - v) / (top - bottom || 1)) * (H - PAD * 1.5);
	const path = $derived(curve.map((p, i) => `${i ? 'L' : 'M'}${x(p.s).toFixed(1)},${y(p.pnl).toFixed(1)}`).join(' '));
	const strikes = $derived([...new Set(widget.legs.filter((l) => l.kind !== 'perp').map((l) => l.strike ?? 0))]);
	const bes = $derived(breakevens(widget.legs));
	const fmt = (v: number) => fmtNum(v, L, v >= 1000 ? 0 : 2);
	const bound = (v: number | null) => (v == null ? t('academy.payoff.unbounded') : fmt(v));
	const legText = $derived(
		widget.legs
			.map((l) =>
				t(l.kind === 'perp' ? 'academy.payoff.perp' : 'academy.payoff.option', {
					side: t(l.side === 'buy' ? 'academy.payoff.buy' : 'academy.payoff.sell'),
					qty: fmtNum(l.qty, L, 0),
					kind: l.kind === 'call' ? 'call' : 'put',
					strike: fmt(l.strike ?? 0),
					price: fmt(l.price)
				})
			)
			.join(' · ')
	);
</script>

<figure class="payoff" data-testid="lesson-payoff">
	<figcaption>
		<span>{t('academy.market', { market: widget.market, interval: t('academy.payoff.atExpiry') })}</span>
		<span class="tag attention">{t('academy.illustrative')}</span>
	</figcaption>
	<p class="legs">{legText}</p>
	<svg viewBox="0 0 {W} {H}" role="img" aria-label={t('academy.payoff.aria', { legs: legText })}>
		<line class="zero" x1={PAD} x2={W - PAD} y1={y(0)} y2={y(0)} />
		{#each strikes as k (k)}
			<line class="strike" x1={x(k)} x2={x(k)} y1={PAD / 2} y2={H - PAD} />
			<text class="axis" x={x(k)} y={H - 6} text-anchor="middle">{fmt(k)}</text>
		{/each}
		{#if !widget.hideAnalytics}
			{#each bes as b (b)}
				<circle class="be" cx={x(b)} cy={y(0)} r="3.5" />
			{/each}
		{/if}
		<path class="curve" d={path} />
		<text class="axis" x={PAD} y={H - 6}>{fmt(widget.lo)}</text>
		<text class="axis" x={W - PAD} y={H - 6} text-anchor="end">{fmt(widget.hi)}</text>
	</svg>
	{#if !widget.hideAnalytics}
		<dl class="kv" data-testid="payoff-analytics">
			<dt><Term id="breakeven">{t('academy.payoff.breakeven')}</Term></dt><dd>{bes.map(fmt).join(' · ') || '—'}</dd>
			<dt>{t('academy.payoff.maxProfit')}</dt><dd class="up">{bound(maxProfit(widget.legs))}</dd>
			<dt><Term id="defined-risk">{t('academy.payoff.maxLoss')}</Term></dt><dd class="down">{bound(maxLoss(widget.legs))}</dd>
		</dl>
	{/if}
	<p class="muted small">{t('academy.payoff.note')}</p>
</figure>

<style>
	.payoff {
		margin: 0.6rem 0;
		border: 1px solid var(--line);
		padding: 0.4rem;
		background: var(--bg);
	}
	figcaption {
		display: flex;
		justify-content: space-between;
		font-size: 0.75rem;
		color: var(--muted);
	}
	.legs {
		font-size: 0.8rem;
		margin: 0.2rem 0;
		font-family: var(--font-mono);
	}
	svg {
		width: 100%;
		height: auto;
		display: block;
	}
	.zero {
		stroke: var(--line);
	}
	.strike {
		stroke: var(--line);
		stroke-dasharray: 3 3;
	}
	.curve {
		fill: none;
		stroke: var(--accent);
		stroke-width: 2;
	}
	.be {
		fill: var(--info);
	}
	.axis {
		fill: var(--muted);
		font-size: 11px;
		font-family: var(--font-mono);
	}
	.kv {
		grid-template-columns: auto 1fr;
	}
	.small {
		font-size: 0.8rem;
		margin: 0.2rem 0 0;
	}
</style>
