// Lower-pane oscillators for the candle chart (spec terminal-ui "Indicator panes").
// Pure Canvas2D drawing over engine-computed series; nothing here derives indicator values.

import type { Indicators } from '#lib/engine/types.ts';

export type Oscillator = 'none' | 'rsi' | 'macd' | 'adx';

export interface Pane {
	top: number;
	h: number;
	width: number; // plot width (excludes the price axis)
	first: number; // first visible bar index
	count: number; // visible bars
	x: (i: number) => number; // visible index -> x
	slotW: number;
}

export interface Palette {
	up: string;
	down: string;
	info: string;
	attention: string;
	muted: string;
	line: string;
}

type Values = (string | null)[];

function yOf(p: Pane, lo: number, hi: number) {
	return (v: number) => p.top + Math.round(((hi - v) / (hi - lo || 1)) * (p.h - 4)) + 2.5;
}

function guide(ctx: CanvasRenderingContext2D, p: Pane, y: number, color: string, label: string) {
	ctx.strokeStyle = color;
	ctx.globalAlpha = 0.5;
	ctx.setLineDash([3, 4]);
	ctx.beginPath();
	ctx.moveTo(0, y);
	ctx.lineTo(p.width, y);
	ctx.stroke();
	ctx.setLineDash([]);
	ctx.globalAlpha = 1;
	ctx.fillStyle = color;
	ctx.fillText(label, p.width + 6, y + 4);
}

function series(ctx: CanvasRenderingContext2D, p: Pane, values: Values, y: (v: number) => number, color: string) {
	ctx.strokeStyle = color;
	ctx.beginPath();
	let started = false;
	for (let i = 0; i < p.count; i++) {
		const v = values[p.first + i];
		if (v == null) {
			started = false;
			continue;
		}
		if (started) ctx.lineTo(p.x(i), y(Number(v)));
		else ctx.moveTo(p.x(i), y(Number(v)));
		started = true;
	}
	ctx.stroke();
}

function frame(ctx: CanvasRenderingContext2D, p: Pane, c: Palette, title: string) {
	ctx.strokeStyle = c.line;
	ctx.beginPath();
	ctx.moveTo(0, p.top - 0.5);
	ctx.lineTo(p.width, p.top - 0.5);
	ctx.stroke();
	ctx.font = '11px "IBM Plex Mono", monospace';
	ctx.fillStyle = c.muted;
	ctx.fillText(title, 4, p.top + 12);
}

function visibleRange(p: Pane, lists: Values[]): [number, number] {
	let lo = Infinity;
	let hi = -Infinity;
	for (const list of lists) {
		for (let i = 0; i < p.count; i++) {
			const v = list[p.first + i];
			if (v == null) continue;
			lo = Math.min(lo, Number(v));
			hi = Math.max(hi, Number(v));
		}
	}
	return Number.isFinite(lo) ? [lo, hi] : [-1, 1];
}

export function drawRsi(ctx: CanvasRenderingContext2D, p: Pane, ind: Indicators, c: Palette) {
	frame(ctx, p, c, 'RSI 14');
	const y = yOf(p, 0, 100);
	guide(ctx, p, y(70), c.down, '70');
	guide(ctx, p, y(30), c.up, '30');
	series(ctx, p, ind.rsi14, y, c.info);
}

export function drawMacd(ctx: CanvasRenderingContext2D, p: Pane, ind: Indicators, c: Palette) {
	frame(ctx, p, c, 'MACD 12 26 9');
	const [lo, hi] = visibleRange(p, [ind.macd, ind.macdSignal, ind.macdHist]);
	const m = Math.max(Math.abs(lo), Math.abs(hi)) * 1.1 || 1;
	const y = yOf(p, -m, m);
	guide(ctx, p, y(0), c.muted, '0');
	const barW = Math.max(1, Math.round(p.slotW * 0.6));
	for (let i = 0; i < p.count; i++) {
		const v = ind.macdHist[p.first + i];
		if (v == null) continue;
		const h = Number(v);
		ctx.fillStyle = h >= 0 ? c.up : c.down;
		ctx.globalAlpha = 0.45;
		const top = Math.min(y(h), y(0));
		ctx.fillRect(Math.round(p.x(i) - barW / 2), top, barW, Math.max(1, Math.abs(y(h) - y(0))));
	}
	ctx.globalAlpha = 1;
	series(ctx, p, ind.macd, y, c.info);
	series(ctx, p, ind.macdSignal, y, c.attention);
}

export function drawAdx(ctx: CanvasRenderingContext2D, p: Pane, ind: Indicators, c: Palette) {
	frame(ctx, p, c, 'ADX 14  +DI  −DI');
	const [, hi] = visibleRange(p, [ind.adx14, ind.plusDi14, ind.minusDi14]);
	const y = yOf(p, 0, Math.max(50, hi * 1.05));
	guide(ctx, p, y(25), c.muted, '25');
	series(ctx, p, ind.plusDi14, y, c.up);
	series(ctx, p, ind.minusDi14, y, c.down);
	ctx.lineWidth = 2;
	series(ctx, p, ind.adx14, y, c.attention);
	ctx.lineWidth = 1;
}

export const OSCILLATORS: Record<Exclude<Oscillator, 'none'>, typeof drawRsi> = { rsi: drawRsi, macd: drawMacd, adx: drawAdx };
