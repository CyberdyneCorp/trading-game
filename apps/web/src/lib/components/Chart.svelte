<script lang="ts">
	// Retro candle chart on Canvas2D (design add-web-client D3). Only closed bars exist here:
	// the area right of the last bar is drawn as an explicit "unknown future" zone.
	import type { GameState } from '#lib/engine/types.ts';
	import { fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { game }: { game: GameState } = $props();

	let canvas: HTMLCanvasElement | undefined = $state();
	let box: HTMLDivElement | undefined = $state();
	let width = $state(800);
	let height = $state(400);
	let hover = $state<number | null>(null);
	let zoom = $state(100); // visible bars

	const FUTURE_SLOTS = 12;
	const AXIS_W = 72;

	interface Level {
		price: number;
		label: string;
		color: string;
		dash: number[];
	}

	const bars = $derived(game.bars.map((b) => b.slice(0, 5).map(Number) as [number, number, number, number, number]));
	const first = $derived(Math.max(0, bars.length - zoom));
	const visible = $derived(bars.slice(first));

	const futureSlots = () => (game.status === 'active' ? FUTURE_SLOTS : 1);

	function cssVar(name: string): string {
		return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#fff';
	}

	function levels(): Level[] {
		const out: Level[] = [];
		const pos = Number(game.position.qty);
		if (pos !== 0) out.push({ price: Number(game.position.entry), label: 'ENTRY', color: cssVar('--info'), dash: [] });
		if (game.account.liquidationPrice) {
			out.push({ price: Number(game.account.liquidationPrice), label: 'LIQ', color: cssVar('--danger'), dash: [6, 3] });
		}
		for (const o of game.orders) {
			if (o.type === 'market') continue;
			const isStop = o.type === 'stop_market';
			out.push({
				price: Number(o.price),
				label: `${isStop ? 'STOP' : o.type === 'take_profit' ? 'TP' : 'LMT'} #${o.id}`,
				color: isStop ? cssVar('--attention') : cssVar('--accent'),
				dash: [3, 3]
			});
		}
		return out;
	}

	function priceRange(lv: Level[]): [number, number] {
		let lo = Infinity;
		let hi = -Infinity;
		for (const b of visible) {
			lo = Math.min(lo, b[2]);
			hi = Math.max(hi, b[1]);
		}
		for (const l of lv) {
			if (l.price > 0 && l.price > lo * 0.8 && l.price < hi * 1.2) {
				lo = Math.min(lo, l.price);
				hi = Math.max(hi, l.price);
			}
		}
		const pad = (hi - lo) * 0.08 || 1;
		return [lo - pad, hi + pad];
	}

	function draw(): void {
		if (!canvas) return;
		const dpr = window.devicePixelRatio || 1;
		canvas.width = Math.floor(width * dpr);
		canvas.height = Math.floor(height * dpr);
		const ctx = canvas.getContext('2d');
		if (!ctx || visible.length === 0) return;
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		ctx.imageSmoothingEnabled = false;

		const lv = levels();
		const [lo, hi] = priceRange(lv);
		const plotW = width - AXIS_W;
		const priceH = Math.floor(height * 0.78);
		const volTop = priceH + 8;
		const volH = height - volTop - 4;
		const slots = visible.length + futureSlots();
		const slotW = plotW / slots;
		const y = (p: number) => Math.round(((hi - p) / (hi - lo)) * (priceH - 8)) + 4.5;
		const x = (i: number) => Math.round(i * slotW + slotW / 2) + 0.5;

		ctx.fillStyle = cssVar('--bg');
		ctx.fillRect(0, 0, width, height);
		drawGrid(ctx, lo, hi, plotW, priceH, y);
		drawWarmup(ctx, slotW, priceH);
		if (game.status === 'active') drawFuture(ctx, visible.length * slotW, plotW, priceH);
		drawVolume(ctx, slotW, volTop, volH, x);
		drawCandles(ctx, slotW, x, y);
		drawFills(ctx, x, y);
		for (const l of lv) drawLevel(ctx, l, plotW, y);
		drawHover(ctx, slotW, priceH, x);
	}

	function drawGrid(ctx: CanvasRenderingContext2D, lo: number, hi: number, plotW: number, priceH: number, y: (p: number) => number) {
		ctx.strokeStyle = cssVar('--line');
		ctx.fillStyle = cssVar('--muted');
		ctx.font = '12px "IBM Plex Mono", monospace';
		ctx.lineWidth = 1;
		const steps = 6;
		for (let i = 0; i <= steps; i++) {
			const p = lo + ((hi - lo) * i) / steps;
			const yy = y(p);
			ctx.globalAlpha = 0.35;
			ctx.beginPath();
			ctx.moveTo(0, yy);
			ctx.lineTo(plotW, yy);
			ctx.stroke();
			ctx.globalAlpha = 1;
			if (yy < priceH) ctx.fillText(p.toFixed(2), plotW + 6, yy + 4);
		}
	}

	function drawWarmup(ctx: CanvasRenderingContext2D, slotW: number, priceH: number) {
		const warmEnd = game.warmupBars - first;
		if (warmEnd <= 0) return;
		ctx.fillStyle = 'rgba(79,216,255,0.04)';
		ctx.fillRect(0, 0, Math.min(warmEnd, visible.length) * slotW, priceH);
	}

	function drawFuture(ctx: CanvasRenderingContext2D, x0: number, plotW: number, priceH: number) {
		ctx.save();
		ctx.beginPath();
		ctx.rect(x0, 0, plotW - x0, priceH);
		ctx.clip();
		ctx.strokeStyle = 'rgba(143,179,161,0.12)';
		for (let i = -priceH; i < plotW; i += 10) {
			ctx.beginPath();
			ctx.moveTo(x0 + i, priceH);
			ctx.lineTo(x0 + i + priceH, 0);
			ctx.stroke();
		}
		ctx.restore();
		ctx.fillStyle = cssVar('--muted');
		ctx.font = '18px VT323, monospace';
		ctx.save();
		ctx.translate(x0 + (plotW - x0) / 2 + 6, priceH / 2);
		ctx.rotate(-Math.PI / 2);
		ctx.textAlign = 'center';
		ctx.fillText(t('chart.future'), 0, 0);
		ctx.restore();
	}

	function drawVolume(ctx: CanvasRenderingContext2D, slotW: number, top: number, h: number, x: (i: number) => number) {
		const maxV = Math.max(...visible.map((b) => b[4]), 1);
		const up = cssVar('--up');
		const down = cssVar('--down');
		visible.forEach((b, i) => {
			const vh = Math.max(1, Math.round((b[4] / maxV) * h));
			ctx.globalAlpha = 0.45;
			ctx.fillStyle = b[3] >= b[0] ? up : down;
			ctx.fillRect(Math.round(x(i) - slotW * 0.35), top + h - vh, Math.max(1, Math.round(slotW * 0.7)), vh);
		});
		ctx.globalAlpha = 1;
	}

	// Up candles hollow, down candles filled: direction is readable without color.
	function drawCandles(ctx: CanvasRenderingContext2D, slotW: number, x: (i: number) => number, y: (p: number) => number) {
		const up = cssVar('--up');
		const down = cssVar('--down');
		const bodyW = Math.max(1, Math.floor(slotW * 0.6));
		visible.forEach(([o, h, l, c], i) => {
			const color = c >= o ? up : down;
			const cx = x(i);
			ctx.strokeStyle = color;
			ctx.fillStyle = color;
			ctx.beginPath();
			ctx.moveTo(cx, y(h));
			ctx.lineTo(cx, y(l));
			ctx.stroke();
			const top = y(Math.max(o, c));
			const bh = Math.max(1, y(Math.min(o, c)) - top);
			const bx = Math.round(cx - bodyW / 2) + 0.5;
			if (c >= o) {
				ctx.fillStyle = cssVar('--bg');
				ctx.fillRect(bx, top, bodyW, bh);
				ctx.strokeRect(bx, top, bodyW, bh);
			} else {
				ctx.fillRect(bx - 0.5, top - 0.5, bodyW + 1, bh + 1);
			}
		});
	}

	function drawFills(ctx: CanvasRenderingContext2D, x: (i: number) => number, y: (p: number) => number) {
		ctx.font = '13px monospace';
		ctx.textAlign = 'center';
		for (const e of game.events) {
			if (e.type !== 'fill' && e.type !== 'liquidation') continue;
			const i = e.step - first;
			if (i < 0 || i >= visible.length) continue;
			const isBuy = e.side === 'buy';
			ctx.fillStyle = e.type === 'liquidation' ? cssVar('--danger') : isBuy ? cssVar('--info') : cssVar('--attention');
			const py = y(Number(e.price));
			ctx.fillText(e.type === 'liquidation' ? '✖' : isBuy ? '▲' : '▼', x(i), isBuy ? py + 14 : py - 6);
		}
		ctx.textAlign = 'start';
	}

	function drawLevel(ctx: CanvasRenderingContext2D, l: Level, plotW: number, y: (p: number) => number) {
		const yy = y(l.price);
		if (yy < 0 || yy > height) return;
		ctx.strokeStyle = l.color;
		ctx.setLineDash(l.dash);
		ctx.beginPath();
		ctx.moveTo(0, yy);
		ctx.lineTo(plotW, yy);
		ctx.stroke();
		ctx.setLineDash([]);
		ctx.font = '11px "IBM Plex Mono", monospace';
		ctx.fillStyle = l.color;
		const w = ctx.measureText(l.label).width;
		ctx.fillText(l.label, plotW - w - 4, yy - 4);
		ctx.fillRect(plotW, yy - 8, AXIS_W, 16);
		ctx.fillStyle = cssVar('--bg');
		ctx.fillText(l.price.toFixed(2), plotW + 6, yy + 4);
	}

	function drawHover(ctx: CanvasRenderingContext2D, slotW: number, priceH: number, x: (i: number) => number) {
		if (hover == null || hover < 0 || hover >= visible.length) return;
		ctx.strokeStyle = 'rgba(227,245,234,0.35)';
		ctx.beginPath();
		ctx.moveTo(x(hover), 0);
		ctx.lineTo(x(hover), priceH);
		ctx.stroke();
		void slotW;
	}

	function onMove(e: MouseEvent) {
		if (!canvas) return;
		const rect = canvas.getBoundingClientRect();
		const slotW = (width - AXIS_W) / (visible.length + futureSlots());
		const i = Math.floor((e.clientX - rect.left) / slotW);
		hover = i >= 0 && i < visible.length ? i : null;
	}

	function onWheel(e: WheelEvent) {
		e.preventDefault();
		zoom = Math.min(Math.max(30, zoom + (e.deltaY > 0 ? 10 : -10)), Math.max(30, bars.length));
	}

	const hoverBar = $derived(hover != null ? visible[hover] : visible[visible.length - 1]);
	const hoverIndex = $derived(hover != null ? first + hover : bars.length - 1);
	const tableRows = $derived(bars.slice(-20).map((b, i) => ({ i: bars.length - 20 + i, b })).filter((r) => r.i >= 0));

	$effect(() => {
		if (!box) return;
		const ro = new ResizeObserver((entries) => {
			const r = entries[0].contentRect;
			width = Math.max(200, Math.floor(r.width));
			height = Math.max(200, Math.floor(r.height));
		});
		ro.observe(box);
		return () => ro.disconnect();
	});

	$effect(() => {
		// Redraw on any dependency change (game, size, hover, zoom, locale).
		void [game, width, height, hover, zoom, i18n.locale, visible];
		draw();
	});
</script>

<div class="chart-wrap">
	<div class="readout num" aria-hidden="true">
		{#if hoverBar}
			<span class="muted">#{hoverIndex}</span>
			O {fmtPrice(String(hoverBar[0]), i18n.locale)}
			H {fmtPrice(String(hoverBar[1]), i18n.locale)}
			L {fmtPrice(String(hoverBar[2]), i18n.locale)}
			C {fmtPrice(String(hoverBar[3]), i18n.locale)}
			<span class="muted">V {fmtQty(String(hoverBar[4]), i18n.locale)}</span>
		{/if}
	</div>
	<div class="canvas-box" bind:this={box}>
		<canvas
			bind:this={canvas}
			style="width:{width}px;height:{height}px"
			onmousemove={onMove}
			onmouseleave={() => (hover = null)}
			onwheel={onWheel}
			aria-hidden="true"
		></canvas>
	</div>
	<table class="sr-only">
		<caption>{t('chart.table')}</caption>
		<thead><tr><th>#</th><th>O</th><th>H</th><th>L</th><th>C</th><th>V</th></tr></thead>
		<tbody>
			{#each tableRows as row (row.i)}
				<tr>
					<td>{row.i}</td><td>{row.b[0]}</td><td>{row.b[1]}</td><td>{row.b[2]}</td><td>{row.b[3]}</td><td>{row.b[4]}</td>
				</tr>
			{/each}
		</tbody>
	</table>
</div>

<style>
	.chart-wrap {
		display: flex;
		flex-direction: column;
		height: 100%;
		min-height: 260px;
	}
	.readout {
		text-align: left;
		font-size: 0.85rem;
		padding: 0 0 0.3rem;
		display: flex;
		gap: 0.8rem;
		flex-wrap: wrap;
	}
	.canvas-box {
		flex: 1;
		min-height: 0;
		position: relative;
		overflow: hidden;
	}
	canvas {
		display: block;
		image-rendering: pixelated;
		cursor: crosshair;
	}
</style>
