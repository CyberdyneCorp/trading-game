<script lang="ts">
	// Small illustrative chart for Academy lessons: candles with volume, bar numbers, an optional
	// highlighted bar, and the unknown future hatched after the last closed bar (nothing drawn there).
	// `anatomy` draws one large candle with its parts named. A screen-reader table lists the bars.
	import type { LessonChart } from '#lib/game/academy.ts';
	import { fmtNum } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { chart }: { chart: LessonChart } = $props();

	const W = 480;
	const PRICE_H = 170;
	const VOL_H = 40;
	const L = $derived(i18n.locale);
	const H = $derived(chart.anatomy ? PRICE_H : PRICE_H + VOL_H + 22);
	const n = $derived(chart.candles.length);
	const known = $derived(chart.known ?? n);
	// Scales from the closed bars only: the future must not leak through the axis.
	const shown = $derived(chart.candles.slice(0, known));
	const slot = $derived((chart.anatomy ? 200 : W - 60) / n);
	const left = $derived(chart.anatomy ? 140 : 8);
	const hi = $derived(Math.max(...shown.map((c) => c.h)));
	const lo = $derived(Math.min(...shown.map((c) => c.l)));
	const maxVol = $derived(Math.max(1, ...shown.map((c) => c.v)));
	const digits = $derived(hi >= 1000 ? 0 : 2);
	const y = (p: number) => 10 + ((hi - p) / (hi - lo || 1)) * (PRICE_H - 20);
	const cx = (i: number) => left + slot * i + slot / 2;
	const fmt = (v: number) => fmtNum(v, L, digits);
	const desc = $derived(t('academy.market', { market: chart.market, interval: chart.interval }));
</script>

<figure class="lesson-chart">
	<figcaption>
		<span>{desc}</span>
		<span class="tag attention">{t('academy.illustrative')}</span>
	</figcaption>
	<svg viewBox="0 0 {W} {H}" role="img" aria-label={desc} data-testid="lesson-chart">
		{#if !chart.anatomy}
			<text class="axis" x={W - 4} y={y(hi) + 4} text-anchor="end">{fmt(hi)}</text>
			<text class="axis" x={W - 4} y={y(lo) + 4} text-anchor="end">{fmt(lo)}</text>
		{/if}
		{#if known < n}
			<defs>
				<pattern id="future-hatch" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
					<line x1="0" y1="0" x2="0" y2="8" class="hatch" />
				</pattern>
			</defs>
			<rect x={left + slot * known} y="0" width={slot * (n - known)} height={PRICE_H + VOL_H} fill="url(#future-hatch)" />
			<text class="future" x={left + slot * (known + (n - known) / 2)} y={PRICE_H / 2} text-anchor="middle">{t('academy.future')}</text>
		{/if}
		{#each shown as c, i (i)}
			{@const up = c.c >= c.o}
			{@const w = Math.max(4, slot * (chart.anatomy ? 0.35 : 0.6))}
			{#if chart.highlight === i + 1}
				<rect class="highlight" x={left + slot * i} y="0" width={slot} height={PRICE_H + VOL_H} />
			{/if}
			<line class:up class:down={!up} x1={cx(i)} x2={cx(i)} y1={y(c.h)} y2={y(c.l)} />
			<rect
				class:up
				class:down={!up}
				x={cx(i) - w / 2}
				y={y(Math.max(c.o, c.c))}
				width={w}
				height={Math.max(1, Math.abs(y(c.o) - y(c.c)))}
			/>
			{#if c.v > 0}
				<rect class="vol" x={cx(i) - w / 2} y={PRICE_H + VOL_H - (c.v / maxVol) * VOL_H} width={w} height={(c.v / maxVol) * VOL_H} />
			{/if}
			{#if !chart.anatomy}
				<text class="axis" x={cx(i)} y={H - 6} text-anchor="middle">{i + 1}</text>
			{/if}
		{/each}
		{#if chart.anatomy}
			{@const c = chart.candles[0]}
			{@const x0 = cx(0) + 45}
			<text class="label" x={x0} y={y(c.h) + 4}>{t('academy.art.high')} {fmt(c.h)}</text>
			<text class="label" x={x0} y={y(c.c) + 4}>{t('academy.art.close')} {fmt(c.c)}</text>
			<text class="label" x={x0} y={y(c.o) + 4}>{t('academy.art.open')} {fmt(c.o)}</text>
			<text class="label" x={x0} y={y(c.l) + 4}>{t('academy.art.low')} {fmt(c.l)}</text>
			<text class="label muted" x={cx(0) - 45} y={(y(c.o) + y(c.c)) / 2 + 4} text-anchor="end">{t('academy.art.body')}</text>
			<text class="label muted" x={cx(0) - 45} y={(y(c.h) + y(c.c)) / 2 + 4} text-anchor="end">{t('academy.art.wick')}</text>
			<text class="label muted" x={cx(0) - 45} y={(y(c.o) + y(c.l)) / 2 + 4} text-anchor="end">{t('academy.art.wick')}</text>
		{/if}
	</svg>
	{#if !chart.anatomy}
		<table class="sr-only">
			<caption>{desc}</caption>
			<thead>
				<tr>
					<th>{t('academy.col.bar')}</th>
					<th>{t('academy.art.open')}</th>
					<th>{t('academy.art.high')}</th>
					<th>{t('academy.art.low')}</th>
					<th>{t('academy.art.close')}</th>
					<th>{t('academy.col.volume')}</th>
				</tr>
			</thead>
			<tbody>
				{#each shown as c, i (i)}
					<tr>
						<td>{i + 1}</td><td>{fmt(c.o)}</td><td>{fmt(c.h)}</td><td>{fmt(c.l)}</td><td>{fmt(c.c)}</td><td>{fmtNum(c.v, L, 0)}</td>
					</tr>
				{/each}
			</tbody>
		</table>
	{/if}
</figure>

<style>
	.lesson-chart {
		margin: 0.6rem 0;
		border: 1px solid var(--line);
		background: var(--bg);
		padding: 0.4rem;
	}
	figcaption {
		display: flex;
		justify-content: space-between;
		font-size: 0.75rem;
		color: var(--muted);
	}
	svg {
		width: 100%;
		height: auto;
		display: block;
	}
	line.up,
	rect.up {
		stroke: var(--up);
		fill: var(--up);
	}
	line.down,
	rect.down {
		stroke: var(--down);
		fill: var(--down);
	}
	line {
		stroke-width: 1.5;
	}
	.vol {
		fill: var(--line);
	}
	.highlight {
		fill: var(--info);
		opacity: 0.12;
	}
	.hatch {
		stroke: var(--line);
		stroke-width: 2;
	}
	.axis,
	.label {
		fill: var(--muted);
		font-size: 11px;
		font-family: var(--font-mono);
	}
	.label {
		fill: var(--text);
		font-size: 13px;
	}
	.label.muted {
		fill: var(--muted);
	}
	.future {
		fill: var(--muted);
		font-size: 12px;
		text-transform: uppercase;
	}
</style>
