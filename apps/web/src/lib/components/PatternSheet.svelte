<script lang="ts">
	// Candlestick pattern cheat sheet (spec terminal-ui "Candle pattern cheat sheet").
	import { ALL_CANDLE_PATTERNS, type PatternBias } from '#lib/game/candlePatterns.ts';
	import { FRAMES, STILL } from '#lib/game/patternOutcome.ts';
	import PatternAnimation from './PatternAnimation.svelte';
	import { CHART_PATTERNS, type ChartPattern } from '#lib/game/chartPatterns.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { onClose, focus = null }: { onClose: () => void; focus?: string | null } = $props();

	let filter = $state<PatternBias | 'all'>('all');
	let tab = $state<'candles' | 'chart'>('candles');
	const shown = $derived(ALL_CANDLE_PATTERNS.filter((p) => filter === 'all' || p.bias === filter));
	const shownChart = $derived(CHART_PATTERNS.filter((p) => filter === 'all' || p.bias === filter));

	// Opened from a chart marker: show that card.
	$effect(() => {
		if (!focus) return;
		document.querySelector(`[data-testid="pattern-${focus}"]`)?.scrollIntoView({ block: 'center' });
	});

	const cy = (v: number) => 100 - v; // chart-pattern drawings: y up
	function pathOf(p: ChartPattern): string {
		return p.path.map(([x, y], i) => `${i ? 'L' : 'M'}${x},${cy(y)}`).join(' ');
	}
	const filters: (PatternBias | 'all')[] = ['all', 'bullish', 'bearish', 'neutral'];

	// One shared clock for every drawing; a still picture when the viewer prefers reduced motion.
	const FRAME_MS = 700;
	let tick = $state(0);
	const frame = $derived(tick < 0 ? STILL : FRAMES[tick % FRAMES.length]);
	$effect(() => {
		if (matchMedia('(prefers-reduced-motion: reduce)').matches) {
			tick = -1;
			return;
		}
		const id = setInterval(() => tick++, FRAME_MS);
		return () => clearInterval(id);
	});
</script>

<div class="modal-backdrop" role="presentation" onclick={onClose}>
	<div
		class="modal sheet"
		role="dialog"
		aria-modal="true"
		aria-labelledby="patterns-title"
		tabindex="-1"
		onclick={(e) => e.stopPropagation()}
		onkeydown={(e) => e.key === 'Escape' && onClose()}
	>
		<header>
			<h2 id="patterns-title">{t('patterns.title')}</h2>
			<button onclick={onClose} aria-label={t('patterns.close')}>✕</button>
		</header>
		<div class="filters" role="tablist">
			<button role="tab" aria-selected={tab === 'candles'} class:active={tab === 'candles'} onclick={() => (tab = 'candles')}>
				{t('patterns.tabCandles')}
			</button>
			<button role="tab" aria-selected={tab === 'chart'} class:active={tab === 'chart'} onclick={() => (tab = 'chart')}>
				{t('patterns.tabChart')}
			</button>
		</div>
		<div class="filters" role="radiogroup" aria-label={t('patterns.filter')}>
			{#each filters as f (f)}
				<button role="radio" aria-checked={filter === f} class:active={filter === f} onclick={() => (filter = f)}>
					{t(`patterns.${f}`)}
				</button>
			{/each}
		</div>
		<p class="note attention">⚠ {t(tab === 'candles' ? 'patterns.note' : 'patterns.chartNote')}</p>
		{#if tab === 'candles'}<p class="note muted">{t('patterns.animNote')}</p>{/if}
		{#if tab === 'chart'}
			<ul class="grid">
				{#each shownChart as p (p.id)}
					{@const tx = p.text[i18n.locale]}
					<li class="card {p.bias}" data-testid="pattern-{p.id}">
						<svg viewBox="-2 -2 104 104" width="110" height="96" aria-hidden="true" class="chartpat">
							{#each p.lines as [x1, y1, x2, y2], i (i)}
								<line class="ref" x1={x1} y1={cy(y1)} x2={x2} y2={cy(y2)} />
							{/each}
							<path class="price" d={pathOf(p)} />
							<line class="arrow {p.bias}" x1={p.breakout[0]} y1={cy(p.breakout[1])} x2={p.breakout[2]} y2={cy(p.breakout[3])} />
							<circle class="arrow-head {p.bias}" cx={p.breakout[2]} cy={cy(p.breakout[3])} r="3" />
						</svg>
						<div class="body">
							<h3>{tx.name} <span class="tag">{t(`patterns.${p.bias}`)}</span></h3>
							<p><b>{t('patterns.what')}</b> {tx.what}</p>
							<p><b>{t('patterns.means')}</b> {tx.means}</p>
							<p><b>{t('patterns.confirm')}</b> {tx.confirm}</p>
						</div>
					</li>
				{/each}
			</ul>
		{:else}
		<ul class="grid">
			{#each shown as p (p.id)}
				{@const tx = p.text[i18n.locale]}
				<li class="card {p.bias}" class:focused={focus === p.id} data-testid="pattern-{p.id}">
					<PatternAnimation pattern={p} {frame} />
					<div class="body">
						<h3>{tx.name} <span class="tag">{t(`patterns.${p.bias}`)}</span></h3>
						<p><b>{t('patterns.what')}</b> {tx.what}</p>
						<p><b>{t('patterns.means')}</b> {tx.means}</p>
						<p><b>{t('patterns.confirm')}</b> {tx.confirm}</p>
					</div>
				</li>
			{/each}
		</ul>
		{/if}
	</div>
</div>

<style>
	.modal.sheet {
		width: min(1100px, calc(100vw - 32px));
		max-width: none;
		max-height: calc(100vh - 48px);
		overflow: auto;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
	}
	.filters {
		display: flex;
		gap: 0.3rem;
		flex-wrap: wrap;
	}
	.filters button:not(.active) {
		opacity: 0.55;
	}
	.note {
		font-size: 0.8rem;
	}
	.grid {
		list-style: none;
		padding: 0;
		margin: 0;
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
		gap: 0.6rem;
	}
	.card {
		display: flex;
		gap: 0.6rem;
		border: 1px solid var(--line);
		padding: 0.5rem;
		background: var(--panel-2);
	}
	.card.bullish {
		border-left: 3px solid var(--up);
	}
	.card.bearish {
		border-left: 3px solid var(--down);
	}
	.card.neutral {
		border-left: 3px solid var(--attention);
	}
	svg {
		flex: none;
		align-self: center;
	}
	.card.focused {
		outline: 2px solid var(--info);
	}
	.chartpat .ref {
		stroke: var(--muted);
		stroke-dasharray: 3 3;
		stroke-width: 1;
	}
	.chartpat .price {
		fill: none;
		stroke: var(--text);
		stroke-width: 2;
	}
	.chartpat .arrow {
		stroke-width: 2;
		stroke-dasharray: 4 2;
	}
	.chartpat .bullish {
		stroke: var(--up);
		fill: var(--up);
	}
	.chartpat .bearish {
		stroke: var(--down);
		fill: var(--down);
	}
	.chartpat .neutral {
		stroke: var(--attention);
		fill: var(--attention);
	}
	h3 {
		margin: 0 0 0.25rem;
		font-size: 0.95rem;
	}
	.tag {
		font-size: 0.7rem;
		color: var(--muted);
		border: 1px solid var(--line);
		padding: 0 0.3rem;
		margin-left: 0.3rem;
	}
	.body p {
		margin: 0.15rem 0;
		font-size: 0.78rem;
	}
	.body b {
		color: var(--muted);
		font-weight: normal;
	}
</style>
