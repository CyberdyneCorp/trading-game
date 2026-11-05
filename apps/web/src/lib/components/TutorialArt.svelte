<script lang="ts">
	// Small schematic drawings for tutorial cards (one per art key), in the terminal palette.
	import type { TutorialArt } from '#lib/game/tutorials.ts';
	import { t } from '#lib/game/i18n.svelte.ts';

	let { art }: { art: TutorialArt } = $props();

	// [open, high, low, close] on a 0..100 scale (y up), drawn left to right.
	type C = [number, number, number, number];
	const y = (v: number) => 92 - v * 0.84;
	const RANGE: C[] = [[40, 52, 34, 48], [48, 56, 42, 44], [44, 50, 36, 38], [38, 50, 34, 47], [47, 58, 44, 55], [55, 60, 46, 49]];
	const TREND: C[] = [[20, 30, 16, 28], [28, 40, 26, 38], [38, 42, 30, 33], [33, 48, 31, 46], [46, 60, 44, 57], [57, 60, 48, 51], [51, 70, 50, 68]];
	const SQUEEZE: C[] = [[50, 62, 38, 41], [41, 55, 36, 52], [52, 56, 46, 48], [48, 52, 46, 50], [50, 52, 48, 49], [49, 51, 48, 50], [50, 74, 49, 72]];
</script>

{#snippet candles(cs: C[], x0: number, step: number)}
	{#each cs as [o, h, l, c], i (i)}
		{@const x = x0 + i * step}
		<g class={c >= o ? 'up' : 'down'}>
			<line x1={x} x2={x} y1={y(h)} y2={y(l)} />
			<rect x={x - 4} y={y(Math.max(o, c))} width="8" height={Math.max(1.5, Math.abs(o - c) * 0.84)} />
		</g>
	{/each}
{/snippet}

<svg viewBox="0 0 160 100" width="240" height="150" class="art" aria-hidden="true" data-art={art}>
	{#if art === 'chart'}
		{@render candles(RANGE, 14, 15)}
		<rect class="future" x="104" y="6" width="50" height="88" />
		<text x="129" y="54" class="small" text-anchor="middle">?</text>
	{:else if art === 'ticket'}
		<rect class="box up-stroke" x="10" y="22" width="64" height="26" />
		<text x="42" y="40" class="up-fill" text-anchor="middle">▲ LONG</text>
		<rect class="box down-stroke" x="86" y="22" width="64" height="26" />
		<text x="118" y="40" class="down-fill" text-anchor="middle">▼ SHORT</text>
		<text x="80" y="76" class="small muted" text-anchor="middle">MKT · LMT · STOP</text>
	{:else if art === 'risk'}
		{@render candles(TREND.slice(0, 5), 16, 14)}
		<line class="entry" x1="8" x2="152" y1={y(46)} y2={y(46)} />
		<line class="stop" x1="8" x2="152" y1={y(26)} y2={y(26)} />
		<text x="150" y={y(46) - 3} class="small info" text-anchor="end">ENTRY</text>
		<text x="150" y={y(26) - 3} class="small attention" text-anchor="end">SL · 1%</text>
		<line class="bracket" x1="104" x2="104" y1={y(46)} y2={y(26)} />
	{:else if art === 'news'}
		{@render candles([[60, 66, 56, 62], [62, 66, 58, 60]], 18, 16)}
		<text x="62" y="20" class="attention">!</text>
		{@render candles([[34, 38, 22, 26], [26, 32, 20, 30]], 74, 16)}
		<line class="gap" x1="40" x2="74" y1={y(58)} y2={y(37)} />
		<text x="58" y="88" class="small muted" text-anchor="middle">gap</text>
		<rect class="box" x="112" y="14" width="40" height="30" />
		<line class="muted-line" x1="118" x2="146" y1="23" y2="23" />
		<line class="muted-line" x1="118" x2="140" y1="31" y2="31" />
		<line class="muted-line" x1="118" x2="144" y1="38" y2="38" />
	{:else if art === 'plan'}
		<rect class="box" x="40" y="10" width="80" height="80" />
		<text x="50" y="32" class="small up-fill">✓ {t('tut.art.hypothesis')}</text>
		<text x="50" y="52" class="small attention">✗ {t('tut.art.invalidation')}</text>
		<text x="50" y="72" class="small info">% {t('tut.art.confidence')}</text>
	{:else if art === 'funding'}
		<rect class="box up-stroke" x="12" y="30" width="44" height="40" />
		<text x="34" y="55" class="up-fill" text-anchor="middle">L</text>
		<rect class="box down-stroke" x="104" y="30" width="44" height="40" />
		<text x="126" y="55" class="down-fill" text-anchor="middle">S</text>
		<line class="arrow" x1="60" x2="98" y1="50" y2="50" />
		<path class="arrow-head" d="M98,50 l-7,-4 v8 z" />
		<text x="80" y="42" class="small attention" text-anchor="middle">+0.01%</text>
	{:else if art === 'trend'}
		{@render candles(TREND, 14, 18)}
		<path class="avg" d="M10,{y(18)} C50,{y(28)} 90,{y(40)} 150,{y(56)}" />
	{:else if art === 'leverage'}
		{@render candles(RANGE.slice(0, 5), 16, 15)}
		<line class="entry" x1="8" x2="152" y1={y(47)} y2={y(47)} />
		<line class="liq" x1="8" x2="152" y1={y(30)} y2={y(30)} />
		<text x="150" y={y(30) - 3} class="small danger" text-anchor="end">LIQ 20×</text>
		<text x="150" y={y(47) - 3} class="small info" text-anchor="end">ENTRY</text>
	{:else if art === 'volatility'}
		{@render candles(SQUEEZE, 14, 19)}
		<path class="band" d="M8,{y(66)} C50,{y(60)} 90,{y(54)} 128,{y(53)}" />
		<path class="band" d="M8,{y(32)} C50,{y(40)} 90,{y(46)} 128,{y(47)}" />
	{:else if art === 'margin'}
		<rect class="box" x="20" y="20" width="40" height="64" />
		<rect class="fill-low" x="20" y="60" width="40" height="24" />
		<text x="40" y="14" class="small muted" text-anchor="middle">{t('tut.art.margin')}</text>
		<line class="liq" x1="70" x2="150" y1="62" y2="62" />
		<text x="150" y="56" class="small danger" text-anchor="end">LIQ</text>
		<text x="40" y="54" class="up-fill" text-anchor="middle">+</text>
	{:else if art === 'payoff'}
		<line class="axis" x1="10" x2="150" y1="70" y2="70" />
		<path class="payoff" d="M10,80 L80,80 L150,20" />
		<line class="strike" x1="80" x2="80" y1="10" y2="92" />
		<text x="84" y="94" class="small muted">strike</text>
		<text x="12" y="92" class="small down-fill">−{t('tut.art.premium')}</text>
	{:else if art === 'chain'}
		{#each [0, 1, 2, 3, 4] as r (r)}
			<rect class="row" class:atm={r === 2} x="14" y={12 + r * 16} width="132" height="13" />
			<text x="30" y={22 + r * 16} class="small up-fill">{(0.8 + (4 - r) * 0.6).toFixed(1)}</text>
			<text x="80" y={22 + r * 16} class="small" text-anchor="middle">{90 + r * 5}</text>
			<text x="124" y={22 + r * 16} class="small down-fill">{(0.8 + r * 0.6).toFixed(1)}</text>
		{/each}
	{:else if art === 'advisor'}
		<rect class="box" x="14" y="12" width="94" height="76" />
		<text x="22" y="32" class="up-fill">▲ LONG</text>
		<text x="100" y="32" class="small muted" text-anchor="end">58%</text>
		<line class="muted-line" x1="22" x2="98" y1="46" y2="46" />
		<line class="muted-line" x1="22" x2="90" y1="56" y2="56" />
		<line class="muted-line" x1="22" x2="94" y1="66" y2="66" />
		<text x="22" y="82" class="small muted">{t('tut.art.reasons')}</text>
		<circle class="coin" cx="134" cy="50" r="16" />
		<text x="134" y="54" class="small attention" text-anchor="middle">1 pt</text>
	{:else if art === 'decay'}
		<line class="axis" x1="10" x2="150" y1="88" y2="88" />
		<path class="payoff" d="M10,18 C80,26 120,44 150,86" />
		<text x="150" y="16" class="small attention" text-anchor="end">θ</text>
		<text x="12" y="98" class="small muted">{t('tut.art.today')}</text>
		<text x="150" y="98" class="small muted" text-anchor="end">{t('tut.art.expiry')}</text>
	{:else if art === 'calendar'}
		<rect class="box" x="30" y="10" width="100" height="80" />
		<line class="muted-line" x1="30" x2="130" y1="26" y2="26" />
		{#each [0, 1, 2, 3] as r (r)}
			{#each [0, 1, 2, 3, 4] as c (c)}
				<rect class={r === 2 && c === 3 ? 'cell mark' : 'cell'} x={36 + c * 19} y={31 + r * 14} width="15" height="10" />
			{/each}
		{/each}
		<text x="99" y="72" class="small attention" text-anchor="middle">%</text>
		<text x="140" y="70" class="small muted">?</text>
	{:else if art === 'wire'}
		{@render candles(RANGE.slice(0, 4), 16, 14)}
		<rect class="future" x="74" y="6" width="80" height="88" />
		<rect class="box info-stroke" x="84" y="16" width="62" height="22" />
		<line class="muted-line" x1="90" x2="138" y1="24" y2="24" />
		<line class="muted-line" x1="90" x2="128" y1="31" y2="31" />
		<line class="arrow" x1="84" x2="66" y1="27" y2="27" />
		<path class="arrow-head" d="M60,27 L67,23 L67,31 Z" />
		<text x="114" y="60" class="small info" text-anchor="middle">−1</text>
	{:else if art === 'book'}
		<!-- An order book with few resting quotes: a wide spread, and a big order eats several levels. -->
		{#each [0, 1, 2] as k (k)}
			<rect class="ask" x="44" y={14 + k * 11} width={18 + k * 14} height="8" />
			<rect class="bid" x="44" y={60 + k * 11} width={14 + k * 16} height="8" />
		{/each}
		<line class="spread" x1="40" x2="120" y1="46" y2="46" />
		<line class="spread" x1="40" x2="120" y1="56" y2="56" />
		<text x="124" y="54" class="small attention">{t('tut.art.spread')}</text>
		<line class="arrow" x1="18" x2="38" y1="40" y2="18" />
		<text x="8" y="50" class="small info">MKT</text>
		<text x="124" y="22" class="small muted">{t('tut.art.capacity')}</text>
	{:else if art === 'trail'}
		<!-- A trailing stop ratchets up behind the best close, one bar late, and never moves down. -->
		{@render candles(TREND, 14, 18)}
		<path class="trail" d="M8,{y(10)} H32 V{y(18)} H50 V{y(28)} H86 V{y(34)} H104 V{y(45)} H140 V{y(56)} H156" />
		<text x="150" y={y(62)} class="small attention" text-anchor="end">TRAIL</text>
	{:else if art === 'regime'}
		<!-- The same chart in two regimes: a range (fade the edges), then a trend (follow); ADX turns late. -->
		{@render candles(RANGE.slice(0, 4), 12, 11)}
		{@render candles(TREND.slice(2), 66, 15)}
		<line class="muted-line" x1="58" x2="58" y1="8" y2="84" />
		<text x="30" y="14" class="small info" text-anchor="middle">RANGE</text>
		<text x="110" y="14" class="small up-fill" text-anchor="middle">TREND</text>
		<path class="avg" d="M8,92 C40,94 60,92 80,86 S120,72 156,66" />
		<text x="150" y="96" class="small attention" text-anchor="end">ADX</text>
	{:else if art === 'straddle'}
		<!-- Call + put at one strike: a V that loses at most the premium, with a breakeven on each side. -->
		<line class="axis" x1="8" x2="152" y1="56" y2="56" />
		<path class="payoff" d="M14,14 L80,78 L146,14" />
		<line class="strike" x1="80" x2="80" y1="8" y2="92" />
		<circle class="be" cx="37" cy="56" r="3" />
		<circle class="be" cx="123" cy="56" r="3" />
		<text x="37" y="50" class="small info" text-anchor="middle">BE</text>
		<text x="123" y="50" class="small info" text-anchor="middle">BE</text>
		<text x="84" y="94" class="small muted">strike</text>
		<text x="84" y="76" class="small down-fill">−{t('tut.art.premium')}</text>
		<text x="150" y="98" class="small muted" text-anchor="end">{t('tut.art.breakeven')}</text>
	{:else if art === 'report'}
		<!-- R multiples of a handful of trades: the average is a guess until the sample grows. -->
		<line class="axis" x1="10" x2="150" y1="50" y2="50" />
		{#each [1.8, -1, -1, 2.4, -0.6, -1, 1.2] as r, i (i)}
			<rect class={r >= 0 ? 'bar-up' : 'bar-down'} x={16 + i * 19} y={r >= 0 ? 50 - r * 14 : 50} width="12" height={Math.abs(r) * 14} />
		{/each}
		<line class="avg" x1="10" x2="150" y1="47" y2="47" />
		<text x="150" y="44" class="small attention" text-anchor="end">E[R]</text>
		<text x="150" y="14" class="small attention" text-anchor="end">n = 7 &lt; 30</text>
		<text x="12" y="94" class="small muted">R · {t('tut.art.trades')}</text>
	{/if}
</svg>

<style>
	.art {
		display: block;
		margin: 0 auto;
		font-family: 'IBM Plex Mono', monospace;
		font-size: 12px;
		fill: var(--text);
	}
	.small {
		font-size: 9px;
	}
	.coin {
		fill: none;
		stroke: var(--attention);
		stroke-width: 1.5;
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
	.box {
		fill: none;
		stroke: var(--line);
	}
	.up-stroke {
		stroke: var(--up);
	}
	.down-stroke {
		stroke: var(--down);
	}
	.up-fill {
		fill: var(--up);
	}
	.down-fill {
		fill: var(--down);
	}
	.muted {
		fill: var(--muted);
	}
	.info {
		fill: var(--info);
	}
	.attention {
		fill: var(--attention);
	}
	.danger {
		fill: var(--danger);
	}
	.future {
		fill: var(--line);
		opacity: 0.35;
	}
	.entry,
	.stop,
	.liq,
	.gap,
	.strike,
	.band {
		stroke-dasharray: 3 2;
		stroke-width: 1;
		fill: none;
	}
	.entry {
		stroke: var(--info);
	}
	.stop,
	.bracket {
		stroke: var(--attention);
	}
	.liq {
		stroke: var(--danger);
	}
	.gap,
	.strike,
	.muted-line,
	.axis {
		stroke: var(--muted);
	}
	.band {
		stroke: var(--bb, var(--info));
	}
	.avg {
		fill: none;
		stroke: var(--attention);
		stroke-width: 1.5;
	}
	.arrow {
		stroke: var(--attention);
		stroke-width: 2;
	}
	.arrow-head {
		fill: var(--attention);
	}
	.payoff {
		fill: none;
		stroke: var(--up);
		stroke-width: 2;
	}
	.fill-low {
		fill: var(--up);
		opacity: 0.4;
	}
	.row {
		fill: var(--panel-2, transparent);
		stroke: var(--line);
	}
	.row.atm {
		stroke: var(--accent);
	}
	.cell {
		fill: none;
		stroke: var(--line);
	}
	.cell.mark {
		stroke: var(--attention);
		stroke-width: 1.5;
	}
	.info-stroke {
		stroke: var(--info);
	}
	.ask {
		fill: var(--down);
		opacity: 0.6;
	}
	.bid {
		fill: var(--up);
		opacity: 0.6;
	}
	.spread {
		stroke: var(--attention);
		stroke-dasharray: 2 2;
	}
	.be {
		fill: var(--info);
	}
	.bar-up {
		fill: var(--up);
		opacity: 0.7;
	}
	.bar-down {
		fill: var(--down);
		opacity: 0.7;
	}
	.trail {
		fill: none;
		stroke: var(--attention);
		stroke-width: 1.5;
		stroke-dasharray: 4 2;
	}
</style>
