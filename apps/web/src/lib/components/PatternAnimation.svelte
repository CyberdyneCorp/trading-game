<script lang="ts">
	// One cheat-sheet drawing: trend context, the pattern (shaded), then the frame's outcome candles.
	// The scale covers every candle of both scenarios, so the drawing never moves while it plays.
	import { contextCandles, type Candle, type CandlePattern } from '#lib/game/candlePatterns.ts';
	import { OUTCOME_BARS, outcomes, type Frame } from '#lib/game/patternOutcome.ts';
	import { t } from '#lib/game/i18n.svelte.ts';

	let { pattern, frame }: { pattern: CandlePattern; frame: Frame } = $props();

	const SLOT = 15;
	const H = 110;
	const PAD = 4;

	const ctx = $derived(contextCandles(pattern.context, pattern.candles[0]));
	const scenarios = $derived(outcomes(pattern));
	const all = $derived([...ctx, ...pattern.candles, ...scenarios[0].candles, ...scenarios[1].candles]);
	const hi = $derived(Math.max(...all.map((c) => c[1])));
	const lo = $derived(Math.min(...all.map((c) => c[2])));
	const k = $derived((H - 2 * PAD) / (hi - lo || 1));
	const y = (v: number) => PAD + (hi - v) * k;
	const bodyH = (o: number, c: number) => Math.max(1.5, Math.abs(o - c) * k);
	const cx = (slot: number) => PAD + slot * SLOT + SLOT / 2;

	const first = $derived(ctx.length); // slot of the pattern's first candle
	const after = $derived(first + pattern.candles.length); // slot of the first outcome candle
	const width = $derived(PAD * 2 + (after + OUTCOME_BARS) * SLOT);
	const outcome = $derived(frame.scenario === null ? null : scenarios[frame.scenario]);
	const stage = $derived(outcome ? outcome.kind : 'pattern');
</script>

{#snippet candle(c: Candle, slot: number, cls: string)}
	{@const [o, h, l, cl] = c}
	<g class="{cls} {cl >= o ? 'up' : 'down'}">
		<line x1={cx(slot)} x2={cx(slot)} y1={y(h)} y2={y(l)} />
		<rect x={cx(slot) - 4.5} y={y(Math.max(o, cl))} width="9" height={bodyH(o, cl)} />
	</g>
{/snippet}

<figure class="anim" data-stage={stage}>
	<svg viewBox="0 0 {width} {H}" {width} height={H} aria-hidden="true">
		<rect class="band" x={PAD + first * SLOT} y="0" width={pattern.candles.length * SLOT} height={H} />
		{#each ctx as c, i (i)}{@render candle(c, i, 'ctx')}{/each}
		{#each pattern.candles as c, i (i)}{@render candle(c, first + i, 'pat')}{/each}
		{#if outcome}
			{#key frame.scenario}
				<line class="level {outcome.levelKind}" x1={PAD + first * SLOT} x2={width - PAD} y1={y(outcome.level)} y2={y(outcome.level)} />
			{/key}
			{#each outcome.candles.slice(0, frame.shown) as c, i (`${frame.scenario}-${i}`)}
				{@render candle(c, after + i, i === 0 ? 'next first' : 'next')}
			{/each}
		{/if}
	</svg>
	<figcaption class={stage}>{t(`patterns.stage.${stage}`)}</figcaption>
</figure>

<style>
	.anim {
		flex: none;
		align-self: center;
		margin: 0;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.15rem;
	}
	figcaption {
		font-size: 0.68rem;
		color: var(--muted);
		white-space: nowrap;
	}
	figcaption.confirmed,
	figcaption.breakUp {
		color: var(--up);
	}
	figcaption.failed,
	figcaption.breakDown {
		color: var(--down);
	}
	.band {
		fill: var(--line);
		opacity: 0.35;
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
	g.next {
		animation: appear 0.35s ease-out;
	}
	.level {
		stroke-width: 1;
		stroke-dasharray: 3 2;
		animation: appear 0.35s ease-out;
	}
	.level.trigger {
		stroke: var(--info);
	}
	.level.stop {
		stroke: var(--down);
	}
	@keyframes appear {
		from {
			opacity: 0;
		}
		to {
			opacity: 1;
		}
	}
</style>
