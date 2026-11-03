<script lang="ts">
	// Candlestick pattern cheat sheet (spec terminal-ui "Candle pattern cheat sheet").
	import { CANDLE_PATTERNS, contextCandles, type Candle, type CandlePattern, type PatternBias } from '#lib/game/candlePatterns.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { onClose }: { onClose: () => void } = $props();

	let filter = $state<PatternBias | 'all'>('all');
	const shown = $derived(CANDLE_PATTERNS.filter((p) => filter === 'all' || p.bias === filter));
	const filters: (PatternBias | 'all')[] = ['all', 'bullish', 'bearish', 'neutral'];
	const SLOT = 18;

	function candlesOf(p: CandlePattern): { c: Candle; ctx: boolean }[] {
		return [...contextCandles(p.context, p.candles[0]).map((c) => ({ c, ctx: true })), ...p.candles.map((c) => ({ c, ctx: false }))];
	}

	const H = 96;
	// Fit each drawing to its own price range so small patterns stay legible.
	function scaler(cs: { c: Candle }[]) {
		const hi = Math.max(...cs.map(({ c }) => c[1]));
		const lo = Math.min(...cs.map(({ c }) => c[2]));
		const k = (H - 8) / (hi - lo || 1);
		return { y: (v: number) => 4 + (hi - v) * k, h: (d: number) => Math.max(1.5, d * k) };
	}
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
		<div class="filters" role="radiogroup" aria-label={t('patterns.filter')}>
			{#each filters as f (f)}
				<button role="radio" aria-checked={filter === f} class:active={filter === f} onclick={() => (filter = f)}>
					{t(`patterns.${f}`)}
				</button>
			{/each}
		</div>
		<p class="note attention">⚠ {t('patterns.note')}</p>
		<ul class="grid">
			{#each shown as p (p.id)}
				{@const tx = p.text[i18n.locale]}
				{@const cs = candlesOf(p)}
				{@const sc = scaler(cs)}
				<li class="card {p.bias}" data-testid="pattern-{p.id}">
					<svg viewBox="0 0 {cs.length * SLOT + 8} {H}" width={cs.length * SLOT + 8} height={H} aria-hidden="true">
						{#each cs as { c, ctx }, i (i)}
							{@const [o, h, l, cl] = c}
							{@const cx = 4 + i * SLOT + SLOT / 2}
							{@const up = cl >= o}
							<g class:ctx class:up class:down={!up}>
								<line x1={cx} x2={cx} y1={sc.y(h)} y2={sc.y(l)} />
								<rect x={cx - 5} y={sc.y(Math.max(o, cl))} width="10" height={sc.h(Math.abs(o - cl))} />
							</g>
						{/each}
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
	g line {
		stroke-width: 1.5;
	}
	g.up line,
	g.up rect {
		stroke: var(--up);
		fill: var(--bg);
	}
	g.down line,
	g.down rect {
		stroke: var(--down);
		fill: var(--down);
	}
	g.ctx line,
	g.ctx rect {
		stroke: var(--muted);
		fill: var(--muted);
		opacity: 0.45;
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
