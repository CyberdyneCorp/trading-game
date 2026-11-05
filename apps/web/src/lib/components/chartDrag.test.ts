import { describe, expect, it } from 'vitest';
import { exitOnValidSide, hitTest, isDraggableOrder, sameTarget, stopMovedBeyondLiquidation, type DragZone } from './chartDrag.ts';

const stop: DragZone = { target: { kind: 'draft', which: 'stop' }, price: 98, lineY: 200, tagY: 200 };
const tp: DragZone = { target: { kind: 'order', orderId: 9 }, price: 109, lineY: 80, tagY: 96 }; // tag spread off its line

describe('moving a stop past liquidation', () => {
	// Playtest: dragging a working stop beyond liquidation went through without the confirmation that
	// placing such a stop asks for.
	it('regression: flags a stop moved to or past the liquidation price, on the position’s side', () => {
		expect(stopMovedBeyondLiquidation('sell', 'stop_market', 89, 90)).toBe(true); // long, liq 90
		expect(stopMovedBeyondLiquidation('sell', 'stop_market', 90, 90)).toBe(true);
		expect(stopMovedBeyondLiquidation('sell', 'stop_market', 91, 90)).toBe(false);
		expect(stopMovedBeyondLiquidation('buy', 'stop_market', 111, 110)).toBe(true); // short, liq 110
		expect(stopMovedBeyondLiquidation('buy', 'stop_market', 109, 110)).toBe(false);
		expect(stopMovedBeyondLiquidation('sell', 'take_profit', 80, 90)).toBe(false);
		expect(stopMovedBeyondLiquidation('sell', 'stop_market', 80, null)).toBe(false);
	});
});

describe('chart drag', () => {
	it('grabs a line within a few pixels anywhere in the plot, and its price tag in the axis', () => {
		expect(hitTest([stop, tp], 300, 203, 700)).toEqual(stop.target);
		expect(hitTest([stop, tp], 300, 210, 700)).toBeNull();
		expect(hitTest([stop, tp], 720, 94, 700)).toEqual(tp.target); // on the spread tag
		expect(hitTest([stop, tp], 300, 94, 700)).toBeNull(); // tag height only counts in the axis
	});

	it('picks the nearest line when two are close', () => {
		const near: DragZone = { ...tp, lineY: 204, tagY: 204 };
		expect(hitTest([stop, near], 300, 203, 700)).toEqual(near.target);
	});

	it('only reduce-only stop and take-profit perp orders are draggable', () => {
		expect(isDraggableOrder({ type: 'stop_market', reduceOnly: true, series: 0 })).toBe(true);
		expect(isDraggableOrder({ type: 'take_profit', reduceOnly: true, series: 0 })).toBe(true);
		expect(isDraggableOrder({ type: 'trailing_stop', reduceOnly: true, series: 0 })).toBe(false);
		expect(isDraggableOrder({ type: 'limit', reduceOnly: false, series: 0 })).toBe(false);
		expect(isDraggableOrder({ type: 'stop_market', reduceOnly: false, series: 0 })).toBe(false);
		expect(isDraggableOrder({ type: 'take_profit', reduceOnly: true, series: 3 })).toBe(false);
	});

	it('a moved exit must stay on its side of the mark', () => {
		// Long position: exits are sells; stop below, target above.
		expect(exitOnValidSide('sell', 'stop_market', 99, 100)).toBe(true);
		expect(exitOnValidSide('sell', 'stop_market', 101, 100)).toBe(false);
		expect(exitOnValidSide('sell', 'take_profit', 101, 100)).toBe(true);
		expect(exitOnValidSide('sell', 'take_profit', 99, 100)).toBe(false);
		// Short position: exits are buys; stop above, target below.
		expect(exitOnValidSide('buy', 'stop_market', 101, 100)).toBe(true);
		expect(exitOnValidSide('buy', 'stop_market', 99, 100)).toBe(false);
		expect(exitOnValidSide('buy', 'take_profit', 99, 100)).toBe(true);
		expect(exitOnValidSide('buy', 'take_profit', 101, 100)).toBe(false);
	});

	it('compares targets by kind and identity', () => {
		expect(sameTarget({ kind: 'order', orderId: 9 }, { kind: 'order', orderId: 9 })).toBe(true);
		expect(sameTarget({ kind: 'order', orderId: 9 }, { kind: 'order', orderId: 8 })).toBe(false);
		expect(sameTarget({ kind: 'draft', which: 'tp' }, { kind: 'order', orderId: 9 })).toBe(false);
		expect(sameTarget(null, { kind: 'draft', which: 'tp' })).toBe(false);
	});
});
