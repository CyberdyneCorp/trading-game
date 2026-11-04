import { describe, expect, it } from 'vitest';
import { exitOnValidSide, hitTest, isDraggableOrder, remainingQty, sameTarget, type DragZone } from './chartDrag.ts';

const stop: DragZone = { target: { kind: 'draft', which: 'stop' }, price: 98, lineY: 200, tagY: 200 };
const tp: DragZone = { target: { kind: 'order', orderId: 9 }, price: 109, lineY: 80, tagY: 96 }; // tag spread off its line

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

	it('the replacement keeps only the quantity still working', () => {
		expect(remainingQty({ qty: '51.5', filled: '0' })).toBe('51.5');
		expect(remainingQty({ qty: '51.5', filled: '20.25' })).toBe('31.25');
		expect(remainingQty({ qty: '0.3', filled: '0.1' })).toBe('0.2');
	});

	it('compares targets by kind and identity', () => {
		expect(sameTarget({ kind: 'order', orderId: 9 }, { kind: 'order', orderId: 9 })).toBe(true);
		expect(sameTarget({ kind: 'order', orderId: 9 }, { kind: 'order', orderId: 8 })).toBe(false);
		expect(sameTarget({ kind: 'draft', which: 'tp' }, { kind: 'order', orderId: 9 })).toBe(false);
		expect(sameTarget(null, { kind: 'draft', which: 'tp' })).toBe(false);
	});
});
