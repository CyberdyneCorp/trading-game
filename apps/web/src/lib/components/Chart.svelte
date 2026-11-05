<script lang="ts">
	// Retro candle chart on Canvas2D (design add-web-client D3). Only closed bars exist here:
	// the area right of the last bar is drawn as an explicit "unknown future" zone.
	import type { GameState } from '#lib/engine/types.ts';
	import { fmtNum, fmtPrice, fmtQty } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { OSCILLATORS, type Oscillator, type Palette } from './chartPanes.ts';
	import { candleCells, candleRange, hitsTag, pinOf, priceRange, spreadLabels, type Pin } from './chartScale.ts';
	import { patternName } from '#lib/game/candlePatterns.ts';
	import type { TicketPreview } from './OrderTicket.svelte';
	import { exitOnValidSide, hitTest, isDraggableOrder, sameTarget, type DragTarget, type DragZone } from './chartDrag.ts';

	let {
		game,
		preview = null,
		picking = false,
		onPicked,
		onCancelPick,
		onPattern,
		onDragLevel,
		onMoveOrder
	}: {
		game: GameState;
		preview?: TicketPreview | null;
		picking?: boolean;
		onPicked?: (price: number) => void;
		onCancelPick?: () => void;
		onPattern?: (id: string) => void;
		/** A draft SL?/TP? line was dragged (called while dragging and on release). */
		onDragLevel?: (which: 'stop' | 'tp', price: number) => void;
		/** A working protective order's line was dropped at a new price. */
		onMoveOrder?: (orderId: number, price: number) => void;
	} = $props();

	let canvas: HTMLCanvasElement | undefined = $state();
	let box: HTMLDivElement | undefined = $state();
	let width = $state(800);
	let height = $state(400);
	let hover = $state<number | null>(null);
	let zoom = $state(100); // visible bars
	let showSma = $state(true);
	let showEma = $state(true);
	let showVwap = $state(false);
	let showBb = $state(false);
	let showPatterns = $state(true);
	let osc = $state<Oscillator>('rsi');
	let hoverY = $state<number | null>(null);
	// Price scale of the last frame, used to turn a click into a price while picking.
	let scale = { lo: 0, hi: 1, priceH: 1 };
	// Dragging a stop/target line: the scale is frozen at drag start so the line follows the pointer.
	let drag = $state<{ target: DragTarget; from: number; price: number; lo: number; hi: number } | null>(null);
	let grabbable = $state<DragTarget | null>(null);
	let zones: DragZone[] = [];
	let suppressClick = false;

	// Bars still needed before an indicator has a value (spec indicators "Warm-up").
	function warming(series: (string | null)[], need: number): number {
		const have = series.filter((v) => v != null).length;
		return have > 0 ? 0 : Math.max(0, need - game.bars.length);
	}

	const FUTURE_SLOTS = 12;
	const AXIS_W = 72;

	interface Level {
		price: number;
		label: string;
		color: string;
		dash: number[];
		drag?: DragTarget;
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
			if (o.type === 'market' || o.series) continue; // option limits are premiums, not underlying prices
			const isStop = o.type === 'stop_market' || o.type === 'trailing_stop';
			const kind = { stop_market: 'STOP', trailing_stop: 'TRAIL', take_profit: 'TP', limit: 'LMT' }[o.type];
			out.push({
				price: Number(o.price),
				label: `${kind} #${o.id}`,
				color: isStop ? cssVar('--attention') : cssVar('--accent'),
				dash: [3, 3],
				drag: isDraggableOrder(o) ? { kind: 'order', orderId: o.id } : undefined
			});
		}
		return out.concat(draftLevels());
	}

	// Ticket draft: what the order would look like, drawn faint and labelled with "?".
	function draftLevels(): Level[] {
		if (!preview) return [];
		const out: Level[] = [{ price: preview.entry, label: 'ENTRY?', color: cssVar('--muted'), dash: [1, 4] }];
		if (preview.stop) {
			out.push({ price: preview.stop, label: 'SL?', color: cssVar('--attention'), dash: [1, 4], drag: { kind: 'draft', which: 'stop' } });
		}
		if (preview.takeProfit) {
			out.push({ price: preview.takeProfit, label: 'TP?', color: cssVar('--accent'), dash: [1, 4], drag: { kind: 'draft', which: 'tp' } });
		}
		if (preview.liquidation) out.push({ price: preview.liquidation, label: 'LIQ?', color: cssVar('--danger'), dash: [1, 4] });
		return out;
	}

	// The dragged level follows the pointer; an order dragged to the wrong side of the mark turns red.
	function withDrag(lv: Level[]): Level[] {
		if (!drag) return lv;
		const d = drag;
		return lv.map((l) => {
			if (!sameTarget(l.drag ?? null, d.target)) return l;
			const order = d.target.kind === 'order' ? game.orders.find((o) => o.id === (d.target as { orderId: number }).orderId) : null;
			const bad = order ? !exitOnValidSide(order.side, order.type, d.price, Number(game.account.mark)) : false;
			return { ...l, price: d.price, dash: [], color: bad ? cssVar('--danger') : l.color };
		});
	}

	function palette(): Palette {
		return {
			up: cssVar('--up'),
			down: cssVar('--down'),
			info: cssVar('--info'),
			attention: cssVar('--attention'),
			muted: cssVar('--muted'),
			line: cssVar('--line')
		};
	}

	// Vertical split: price, volume strip, optional oscillator pane.
	function layout() {
		const oscH = osc === 'none' ? 0 : Math.floor(height * 0.24);
		const volH = Math.floor(height * (osc === 'none' ? 0.2 : 0.12));
		const priceH = height - oscH - volH - 8;
		return { priceH, volTop: priceH + 4, volH: volH - 4, oscTop: priceH + volH + 8, oscH: oscH - 4 };
	}

	function scaleFor(lv: Level[]): [number, number] {
		const [lo, hi] = candleRange(visible.map((b) => b[2]), visible.map((b) => b[1]));
		return priceRange(lo, hi, lv.map((l) => l.price));
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

		const lv = withDrag(levels());
		const [lo, hi] = drag ? [drag.lo, drag.hi] : scaleFor(lv);
		const plotW = width - AXIS_W;
		const { priceH, volTop, volH, oscTop, oscH } = layout();
		scale = { lo, hi, priceH };
		const slots = visible.length + futureSlots();
		const slotW = plotW / slots;
		const y = (p: number) => Math.round(((hi - p) / (hi - lo)) * (priceH - 8)) + 4.5;
		const x = (i: number) => Math.round(i * slotW + slotW / 2) + 0.5;

		ctx.fillStyle = cssVar('--bg');
		ctx.fillRect(0, 0, width, height);
		const tags = levelLayout(lv, priceH, y, lo, hi);
		drawGrid(ctx, lo, hi, plotW, priceH, y, tags.tagY);
		drawWarmup(ctx, slotW, priceH);
		if (game.status === 'active') drawFuture(ctx, visible.length * slotW, plotW, priceH);
		drawVolume(ctx, slotW, volTop, volH, x);
		drawCandles(ctx, slotW, x, y);
		drawFills(ctx, x, y);
		drawNews(ctx, x, priceH);
		if (showSma) drawLine(ctx, game.indicators.sma20, cssVar('--attention'), x, y);
		if (showEma) drawLine(ctx, game.indicators.ema50, cssVar('--info'), x, y);
		if (showBb) drawBands(ctx, x, y);
		if (showPatterns) drawPatterns(ctx, slotW, x, y);
		if (showVwap) drawLine(ctx, game.indicators.vwap, cssVar('--vwap'), x, y);
		drawLevels(ctx, tags, plotW);
		if (osc !== 'none') {
			const pane = { top: oscTop, h: oscH, width: plotW, first, count: visible.length, x, slotW };
			OSCILLATORS[osc](ctx, pane, game.indicators, palette());
		}
		drawHover(ctx, slotW, osc === 'none' ? priceH : oscTop + oscH, x);
		drawPick(ctx, plotW, priceH, y);
	}

	function priceAt(py: number): number {
		const { lo, hi, priceH } = scale;
		return hi - ((py - 4.5) / (priceH - 8)) * (hi - lo);
	}

	// While picking a stop/target: horizontal crosshair with the price under the mouse.
	function drawPick(ctx: CanvasRenderingContext2D, plotW: number, priceH: number, y: (p: number) => number) {
		if (!picking || hoverY == null || hoverY > priceH) return;
		const price = priceAt(hoverY);
		const color = cssVar('--attention');
		ctx.strokeStyle = color;
		ctx.setLineDash([4, 2]);
		ctx.beginPath();
		ctx.moveTo(0, y(price));
		ctx.lineTo(plotW, y(price));
		ctx.stroke();
		ctx.setLineDash([]);
		ctx.fillStyle = color;
		ctx.fillRect(plotW, y(price) - 8, AXIS_W, 16);
		ctx.fillStyle = cssVar('--bg');
		ctx.font = '11px "IBM Plex Mono", monospace';
		ctx.fillText(fmtNum(price, i18n.locale), plotW + 6, y(price) + 4);
	}

	// Grid lines with axis prices; a price that would sit under a level tag is left out.
	function drawGrid(ctx: CanvasRenderingContext2D, lo: number, hi: number, plotW: number, priceH: number, y: (p: number) => number, tagY: number[]) {
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
			if (yy < priceH && !hitsTag(yy, tagY)) ctx.fillText(fmtNum(p, i18n.locale), plotW + 6, yy + 4);
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

	function drawLine(ctx: CanvasRenderingContext2D, series: (string | null)[], color: string, x: (i: number) => number, y: (p: number) => number) {
		ctx.strokeStyle = color;
		ctx.globalAlpha = 0.8;
		ctx.beginPath();
		let started = false;
		for (let i = 0; i < visible.length; i++) {
			const v = series[first + i];
			if (v == null) {
				started = false;
				continue;
			}
			if (started) ctx.lineTo(x(i), y(Number(v)));
			else ctx.moveTo(x(i), y(Number(v)));
			started = true;
		}
		ctx.stroke();
		ctx.globalAlpha = 1;
	}

	// Bollinger(20, 2): both bands plus a faint fill between them.
	function drawBands(ctx: CanvasRenderingContext2D, x: (i: number) => number, y: (p: number) => number) {
		const { bbUpper, bbLower } = game.indicators;
		const color = cssVar('--bb');
		ctx.fillStyle = color;
		ctx.globalAlpha = 0.06;
		ctx.beginPath();
		const idx = [...Array(visible.length).keys()].filter((i) => bbUpper[first + i] != null && bbLower[first + i] != null);
		idx.forEach((i, n) => {
			const py = y(Number(bbUpper[first + i]));
			if (n === 0) ctx.moveTo(x(i), py);
			else ctx.lineTo(x(i), py);
		});
		for (const i of [...idx].reverse()) ctx.lineTo(x(i), y(Number(bbLower[first + i])));
		ctx.fill();
		ctx.globalAlpha = 1;
		drawLine(ctx, bbUpper, color, x, y);
		drawLine(ctx, bbLower, color, x, y);
	}

	// Detected candle patterns: a bracket under (bullish) or over (bearish) the bars that form it.
	function drawPatterns(ctx: CanvasRenderingContext2D, slotW: number, x: (i: number) => number, y: (p: number) => number) {
		const colors = { 1: cssVar('--up'), '-1': cssVar('--down'), 0: cssVar('--attention') } as Record<string, string>;
		for (const p of game.indicators.patterns) {
			const end = p.step - first;
			const start = end - p.bars + 1;
			if (end < 0 || start >= visible.length) continue;
			const span = visible.slice(Math.max(0, start), end + 1);
			const below = p.bias >= 0;
			const edge = below ? Math.min(...span.map((b) => b[2])) : Math.max(...span.map((b) => b[1]));
			const yy = y(edge) + (below ? 6 : -6);
			ctx.strokeStyle = colors[String(p.bias)];
			ctx.globalAlpha = 0.85;
			ctx.beginPath();
			ctx.moveTo(x(Math.max(0, start)) - slotW * 0.35, yy);
			ctx.lineTo(x(end) + slotW * 0.35, yy);
			ctx.stroke();
			ctx.beginPath();
			ctx.arc(x(end), yy + (below ? 3 : -3), 2, 0, Math.PI * 2);
			ctx.fillStyle = colors[String(p.bias)];
			ctx.fill();
			ctx.globalAlpha = 1;
		}
	}

	// News markers at release bars: dotted line + "N" flag (released items only exist in the state).
	function drawNews(ctx: CanvasRenderingContext2D, x: (i: number) => number, priceH: number) {
		ctx.font = '12px VT323, monospace';
		for (const n of game.news) {
			const i = n.step - first;
			if (i < 0 || i >= visible.length) continue;
			const color = n.tone > 0 ? cssVar('--up') : n.tone < 0 ? cssVar('--down') : cssVar('--attention');
			ctx.strokeStyle = color;
			ctx.setLineDash([2, 3]);
			ctx.beginPath();
			ctx.moveTo(x(i), 12);
			ctx.lineTo(x(i), priceH);
			ctx.stroke();
			ctx.setLineDash([]);
			ctx.fillStyle = color;
			ctx.fillText('N', x(i) - 3, 10);
		}
	}

	// Lines at their price (or pinned to the edge with an arrow when off-scale); tags spread apart so
	// nearby levels (ENTRY? on ENTRY) stay readable.
	function levelLayout(lv: Level[], priceH: number, y: (p: number) => number, lo: number, hi: number) {
		const shown = lv.filter((l) => l.price > 0);
		const pins = shown.map((l) => pinOf(l.price, lo, hi));
		const lineY = shown.map((l, i) => (pins[i] === 'above' ? 4.5 : pins[i] === 'below' ? priceH - 3.5 : y(l.price)));
		return { shown, pins, lineY, tagY: spreadLabels(lineY, 0, priceH) };
	}

	function drawLevels(ctx: CanvasRenderingContext2D, { shown, pins, lineY, tagY }: ReturnType<typeof levelLayout>, plotW: number) {
		ctx.font = '11px "IBM Plex Mono", monospace';
		shown.forEach((l, i) => drawLevel(ctx, l, plotW, lineY[i], tagY[i], pins[i]));
		zones = shown.flatMap((l, i) => (l.drag ? [{ target: l.drag, price: l.price, lineY: lineY[i], tagY: tagY[i] }] : []));
	}

	function drawLevel(ctx: CanvasRenderingContext2D, l: Level, plotW: number, yy: number, tag: number, pin: Pin) {
		ctx.strokeStyle = l.color;
		ctx.setLineDash(pin ? [2, 6] : l.dash);
		ctx.beginPath();
		ctx.moveTo(pin ? plotW * 0.6 : 0, yy);
		ctx.lineTo(plotW, yy);
		ctx.stroke();
		ctx.setLineDash([]);
		ctx.fillStyle = l.color;
		const name = l.drag ? `⇕ ${l.label}` : l.label;
		const label = pin === 'above' ? `${name} ▲` : pin === 'below' ? `${name} ▼` : name;
		const w = ctx.measureText(label).width;
		ctx.fillText(label, plotW - w - 4, Math.max(tag - 4, 11)); // above its tag, but never off the top
		ctx.fillRect(plotW, tag - 8, AXIS_W, 16);
		ctx.fillStyle = cssVar('--bg');
		ctx.fillText(fmtNum(l.price, i18n.locale), plotW + 6, tag + 4);
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

	function pointerAt(e: MouseEvent): { px: number; py: number } {
		const rect = canvas!.getBoundingClientRect();
		return { px: e.clientX - rect.left, py: e.clientY - rect.top };
	}

	function onDown(e: PointerEvent) {
		if (!canvas || picking || e.button !== 0) return;
		const { px, py } = pointerAt(e);
		const target = hitTest(zones, px, py, width - AXIS_W);
		if (!target) return;
		e.preventDefault();
		canvas.setPointerCapture?.(e.pointerId);
		const from = zones.find((z) => sameTarget(z.target, target))?.price ?? priceAt(py);
		drag = { target, from, price: from, lo: scale.lo, hi: scale.hi };
	}

	function dragTo(py: number) {
		if (!drag) return;
		const price = priceAt(Math.min(Math.max(py, 4), scale.priceH - 4));
		if (!(price > 0)) return;
		drag = { ...drag, price };
		if (drag.target.kind === 'draft') onDragLevel?.(drag.target.which, price);
	}

	function onUp() {
		if (!drag) return;
		const { target, price } = drag;
		drag = null;
		suppressClick = true; // the click that ends a drag is not a bar click
		if (target.kind === 'draft') onDragLevel?.(target.which, price);
		else onMoveOrder?.(target.orderId, price);
	}

	// Abandon a drag: a draft line goes back to where it was; an order was never touched.
	function cancelDrag() {
		if (drag?.target.kind === 'draft') onDragLevel?.(drag.target.which, drag.from);
		drag = null;
	}

	function onMove(e: PointerEvent) {
		if (!canvas) return;
		if (drag) return dragTo(pointerAt(e).py);
		const { px, py } = pointerAt(e);
		grabbable = picking ? null : hitTest(zones, px, py, width - AXIS_W);
		const rect = canvas.getBoundingClientRect();
		const slotW = (width - AXIS_W) / (visible.length + futureSlots());
		const i = Math.floor((e.clientX - rect.left) / slotW);
		hover = i >= 0 && i < visible.length ? i : null;
		hoverY = e.clientY - rect.top;
	}

	function onClick(e: MouseEvent) {
		if (suppressClick) {
			suppressClick = false;
			return;
		}
		if (!picking && hover != null) {
			const hit = patternsAt(first + hover)[0];
			if (hit) onPattern?.(hit.id);
			return;
		}
		if (!picking || !canvas) return;
		const py = e.clientY - canvas.getBoundingClientRect().top;
		if (py > scale.priceH) return;
		const price = priceAt(py);
		if (price > 0) onPicked?.(price);
	}

	function onKeydown(e: KeyboardEvent) {
		if (picking && e.key === 'Escape') onCancelPick?.();
		if (drag && e.key === 'Escape') cancelDrag();
	}

	function onWheel(e: WheelEvent) {
		e.preventDefault();
		zoom = Math.min(Math.max(30, zoom + (e.deltaY > 0 ? 10 : -10)), Math.max(30, bars.length));
	}

	function fmtOsc(v: string | null | undefined, digits: number): string {
		const opts = { minimumFractionDigits: digits, maximumFractionDigits: digits };
		return v != null ? Number(v).toLocaleString(i18n.locale, opts) : '—';
	}

	function patternsAt(step: number) {
		return showPatterns ? game.indicators.patterns.filter((p) => p.step === step) : [];
	}
	const hoverPatterns = $derived(patternsAt(hover != null ? first + hover : bars.length - 1));

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
		void [game, width, height, hover, hoverY, zoom, i18n.locale, visible, showSma, showEma, showVwap, showBb, showPatterns, osc, preview, picking, drag];
		draw();
	});
</script>

<svelte:window onkeydown={onKeydown} />

<div class="chart-wrap">
	<div class="readout num" aria-hidden="true">
		{#if hoverBar}
			<span class="muted">#{hoverIndex}</span>
			O {fmtPrice(String(hoverBar[0]), i18n.locale)}
			H {fmtPrice(String(hoverBar[1]), i18n.locale)}
			L {fmtPrice(String(hoverBar[2]), i18n.locale)}
			C {fmtPrice(String(hoverBar[3]), i18n.locale)}
			<span class="muted">V {fmtQty(String(hoverBar[4]), i18n.locale)}</span>
			{@const rsiV = game.indicators.rsi14[hoverIndex]}
			{@const atrV = game.indicators.atr14[hoverIndex]}
			<span class="info">RSI14 {fmtOsc(rsiV, 1)}</span>
			<span class="muted">ATR14 {atrV != null ? fmtPrice(atrV, i18n.locale) : '—'}</span>
			{@const ind = game.indicators}
			{#if showBb}<span class="bb">BB {ind.bbLower[hoverIndex] != null ? `${fmtPrice(ind.bbLower[hoverIndex]!, i18n.locale)}–${fmtPrice(ind.bbUpper[hoverIndex]!, i18n.locale)}` : '—'}</span>{/if}
			{#if showVwap}<span class="vwap">VWAP {ind.vwap[hoverIndex] != null ? fmtPrice(ind.vwap[hoverIndex]!, i18n.locale) : '—'}</span>{/if}
			{#if osc === 'macd'}
				<span class="muted">MACD {fmtOsc(ind.macd[hoverIndex], 3)} / {fmtOsc(ind.macdSignal[hoverIndex], 3)}</span>
			{:else if osc === 'adx'}
				<span class="muted">ADX {fmtOsc(ind.adx14[hoverIndex], 1)} · +DI {fmtOsc(ind.plusDi14[hoverIndex], 1)} · −DI {fmtOsc(ind.minusDi14[hoverIndex], 1)}</span>
			{/if}
		{/if}
		<label class="toggle attention"><input type="checkbox" bind:checked={showSma} /> SMA20</label>
		<label class="toggle info"><input type="checkbox" bind:checked={showEma} /> EMA50</label>
		<label class="toggle vwap"><input type="checkbox" bind:checked={showVwap} /> VWAP</label>
		<label class="toggle bb"><input type="checkbox" bind:checked={showBb} /> BB20</label>
		<label class="toggle"><input type="checkbox" bind:checked={showPatterns} /> {t('chart.patterns')}</label>
		<label class="toggle">
			<span class="sr-only">{t('chart.osc')}</span>
			<select bind:value={osc} aria-label={t('chart.osc')} data-testid="osc-select">
				<option value="rsi">RSI</option>
				<option value="macd">MACD</option>
				<option value="adx">ADX</option>
				<option value="none">{t('chart.oscNone')}</option>
			</select>
		</label>
		{#if warming(game.indicators.ema50, 50) > 0}
			<span class="muted">{t('chart.warming', { name: 'EMA50', n: warming(game.indicators.ema50, 50) })}</span>
		{/if}
		{#if picking}<span class="attention" role="status">⌖ {t('chart.pickHint')}</span>{/if}
	</div>
	<!-- Always present at a fixed height: showing a pattern must not move or resize the chart. -->
	<div class="pattern-line" data-testid="pattern-line">
		{#if hoverPatterns.length}
			<span class="muted">{t('chart.patternAt')}</span>
			{#each hoverPatterns as p (p.id)}
				<button class="pattern {p.bias > 0 ? 'up' : p.bias < 0 ? 'down' : 'attention'}" onclick={() => onPattern?.(p.id)}>
					{p.bias > 0 ? '▲' : p.bias < 0 ? '▼' : '◆'} {patternName(p.id, i18n.locale)}
				</button>
			{/each}
		{/if}
	</div>
	<div class="canvas-box" class:picking class:grab={grabbable || drag} bind:this={box} data-testid="chart-box">
		<canvas
			bind:this={canvas}
			style="width:{width}px;height:{height}px"
			onpointerdown={onDown}
			onpointermove={onMove}
			onpointerup={onUp}
			onpointercancel={cancelDrag}
			onmouseleave={() => {
				hover = null;
				hoverY = null;
				grabbable = null;
			}}
			onclick={onClick}
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
					<td>{row.i}</td>{#each candleCells(row.b, i18n.locale) as cell, j (j)}<td>{cell}</td>{/each}
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
	.toggle {
		display: inline-flex;
		gap: 0.25rem;
		align-items: center;
		font-size: 0.8rem;
	}
	.toggle input {
		width: auto;
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
	.vwap {
		color: var(--vwap);
	}
	.bb {
		color: var(--bb);
	}
	.pattern-line {
		display: flex;
		gap: 0.4rem;
		align-items: center;
		font-size: 0.78rem;
		flex: none;
		height: 1.6rem;
		overflow: hidden;
		white-space: nowrap;
	}
	.pattern {
		font-size: 0.75rem;
		padding: 0 0.4rem;
		min-height: 0;
	}
	.picking canvas {
		cursor: cell;
	}
	.grab canvas {
		cursor: ns-resize;
	}
	.toggle select {
		font-size: 0.78rem;
		padding: 0 0.3rem;
		min-height: 0;
	}
</style>
