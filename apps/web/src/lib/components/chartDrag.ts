// Dragging stop and target lines on the chart (spec terminal-ui "Stop and target tools").
// Draft lines (SL?, TP?) edit the ticket; working protective orders (STOP #n, TP #n) are moved in
// place by the engine (MoveOrder): same order, same quantity and OCO link, and a rejected move leaves
// the order where it was.

import type { OrderSideName, OrderTypeName, OrderView } from '#lib/engine/types.ts';

export type DragTarget = { kind: 'draft'; which: 'stop' | 'tp' } | { kind: 'order'; orderId: number };

/** Where a draggable level was drawn in the last frame (canvas pixels). */
export interface DragZone {
	target: DragTarget;
	price: number;
	lineY: number;
	tagY: number;
}

export const LINE_GRAB_PX = 5;
const TAG_GRAB_PX = 8;

/** Protective exits the player may drag: reduce-only stop-market and take-profit orders. */
export function isDraggableOrder(o: Pick<OrderView, 'type' | 'reduceOnly' | 'series'>): boolean {
	return o.reduceOnly && !o.series && (o.type === 'stop_market' || o.type === 'take_profit');
}

/** The level under the pointer: on its line anywhere in the plot, or on its price tag in the axis. */
export function hitTest(zones: DragZone[], px: number, py: number, plotW: number): DragTarget | null {
	let best: { target: DragTarget; d: number } | null = null;
	for (const z of zones) {
		const onLine = Math.abs(py - z.lineY);
		const onTag = px >= plotW && Math.abs(py - z.tagY) <= TAG_GRAB_PX ? 0 : Infinity;
		const d = Math.min(onLine <= LINE_GRAB_PX ? onLine : Infinity, onTag);
		if (d !== Infinity && (!best || d < best.d)) best = { target: z.target, d };
	}
	return best?.target ?? null;
}

export function sameTarget(a: DragTarget | null, b: DragTarget | null): boolean {
	if (!a || !b || a.kind !== b.kind) return false;
	return a.kind === 'draft' ? a.which === (b as typeof a).which : a.orderId === (b as typeof a).orderId;
}

/**
 * A moved exit must stay on its side of the mark, or it would trigger at the next open: a sell stop
 * (protecting a long) below the mark, a sell target above it; the reverse for buys.
 */
export function exitOnValidSide(side: OrderSideName, type: OrderTypeName, price: number, mark: number): boolean {
	const below = price < mark;
	const isStop = type === 'stop_market';
	return side === 'sell' ? below === isStop : below !== isStop;
}

/**
 * A stop moved to or past the position's liquidation price (a sell stop protecting a long at or below
 * it, a buy stop at or above it): liquidation would happen first. Like placing such a stop, moving one
 * there needs the player's confirmation when the mission asks for it (MissionRules.confirmStopBeyondLiq).
 */
export function stopMovedBeyondLiquidation(side: OrderSideName, type: OrderTypeName, price: number, liquidation: number | null): boolean {
	if (type !== 'stop_market' || liquidation == null) return false;
	return side === 'sell' ? price <= liquidation : price >= liquidation;
}
