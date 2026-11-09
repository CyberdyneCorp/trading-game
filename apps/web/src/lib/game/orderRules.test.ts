import { beforeAll, describe, expect, it } from 'vitest';
import type { GameState, OrderView } from '#lib/engine/types.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import {
	barsLeft,
	bestClose,
	dayBars,
	linkMark,
	linkPartner,
	linkProblem,
	plannedTrailOrder,
	tifBlock,
	tifCell,
	tifOrder,
	toRaw,
	trailLabel,
	trailTrigger
} from './orderRules.ts';
import { parseDecimal } from './format.ts';
import { trailRateFromPercent } from './stops.ts';

const order = (p: Partial<OrderView>): OrderView =>
	({
		id: 1,
		type: 'limit',
		side: 'buy',
		qty: '1.00000000',
		filled: '0.00000000',
		price: '100.00000000',
		reduceOnly: false,
		status: 'accepted',
		placedStep: 60,
		parentId: 0,
		ocoGroup: 0,
		trailDistance: '0.00000000',
		trailRate: '0',
		bracketStop: '0.00000000',
		bracketTp: '0.00000000',
		series: 0,
		tif: 'gtc',
		tifBars: 0,
		expireStep: 0,
		linkGroup: 0,
		...p
	}) as OrderView;

describe('time in force (design D6)', () => {
	it('a day is 86,400 / stepSeconds bars: 24 hourly bars, 96 of 15 minutes', () => {
		expect(dayBars(3600)).toBe(24);
		expect(dayBars(900)).toBe(96);
		expect(dayBars(86_400 * 2)).toBe(1);
		expect(barsLeft({ totalBars: 200, step: 60 })).toBe(139);
	});

	it('ioc and fok need a limit; bars and day are not for a market order', () => {
		expect(tifBlock('ioc', 'limit')).toBeNull();
		expect(tifBlock('fok', 'stop_market')).toBe('needs_limit');
		expect(tifBlock('ioc', 'market')).toBe('needs_limit');
		expect(tifBlock('day', 'market')).toBe('market_immediate');
		expect(tifBlock('bars', 'stop_market')).toBeNull();
		expect(tifBlock('gtc', 'market')).toBeNull();
	});

	it('the order sent: day = 24 bars, cut to the bars left; N bars within 1..left', () => {
		expect(tifOrder('gtc', 0, 3600, 100)).toEqual({ tif: 'gtc' });
		expect(tifOrder('day', 0, 3600, 100)).toEqual({ tif: 'bars', tifBars: 24 });
		expect(tifOrder('day', 0, 3600, 10)).toEqual({ tif: 'bars', tifBars: 10 });
		expect(tifOrder('bars', 3, 3600, 10)).toEqual({ tif: 'bars', tifBars: 3 });
		expect(tifOrder('bars', 0, 3600, 10)).toEqual({ error: 'bars_range' });
		expect(tifOrder('bars', 11, 3600, 10)).toEqual({ error: 'bars_range' });
		expect(tifOrder('ioc', 0, 3600, 0)).toEqual({ tif: 'ioc' });
	});

	it('the orders panel names the kind and the last bar it can execute on', () => {
		expect(tifCell(order({}))).toEqual({ kind: 'gtc', until: null });
		expect(tifCell(order({ tif: 'bars', tifBars: 3, expireStep: 63 }))).toEqual({ kind: 'bars', until: 63 });
		expect(tifCell(order({ tif: 'ioc', expireStep: 61 }))).toEqual({ kind: 'ioc', until: 61 });
		expect(tifCell({ tif: undefined, tifBars: undefined, expireStep: undefined })).toEqual({ kind: 'gtc', until: null }); // an older build
	});
});

describe('OCO links (design D7)', () => {
	it('mirrors the engine refusals', () => {
		const a = order({ id: 3 });
		expect(linkProblem(a, a)).toBe('same');
		expect(linkProblem(a, order({ id: 4, parentId: 2 }))).toBe('child');
		expect(linkProblem(a, order({ id: 4, linkGroup: 4 }))).toBe('linked');
		expect(linkProblem(a, order({ id: 4 }))).toBeNull();
	});

	it('names the partner on both orders while both work', () => {
		const orders = [order({ id: 3, linkGroup: 3 }), order({ id: 5, linkGroup: 3 }), order({ id: 6 })];
		expect(linkPartner(orders[0], orders)?.id).toBe(5);
		expect(linkMark(orders[1], orders)).toBe('⛓#3');
		expect(linkMark(orders[2], orders)).toBeNull();
		expect(linkMark(orders[0], [orders[0]])).toBeNull(); // the partner ended
	});
});

describe('trailing stop (design D10)', () => {
	it('the trigger: best close minus the distance, or minus ceil(close x rate), outward on the tick', () => {
		// 2% of 103,47 is 2,0694: 103,47 - 2,0694 = 101,4006, down to 101,40 for a long's stop.
		expect(trailTrigger('sell', '103.47', { rate: '0.02' }, '0.01')).toBe('101.40000000');
		// A short's stop goes up: 98,13 + 1,9626 = 100,0926 -> 100,10.
		expect(trailTrigger('buy', '98.13', { rate: '0.02' }, '0.01')).toBe('100.10000000');
		expect(trailTrigger('sell', '103.47', { distance: '1.5' }, '0.01')).toBe('101.97000000');
		expect(trailTrigger('sell', '1', { distance: '5' }, '0.01')).toBe('0.01000000'); // never below one tick
		expect(toRaw('-1.5')).toBe(-150000000n);
	});

	it('the best close since placement: the highest for a long, the lowest for a short', () => {
		const closes = [100, 101, 103.5, 102, 99];
		expect(bestClose(closes, 1, 'sell')).toEqual({ step: 2, close: 103.5 });
		expect(bestClose(closes, 3, 'buy')).toEqual({ step: 4, close: 99 });
		expect(bestClose(closes, 9, 'sell')).toBeNull();
	});

	it('the chart label shows the rule and the trigger', () => {
		expect(trailLabel({ trailRate: '0.0200000000', trailDistance: '0', price: '101.96' }, 'pt-BR')).toBe('TRAIL 2% · 101,96');
		expect(trailLabel({ trailRate: '0.0250000000', trailDistance: '0', price: '101.96' }, 'en')).toBe('TRAIL 2.50% · 101.96');
		expect(trailLabel({ trailRate: '0', trailDistance: '1.50000000', price: '101.96' }, 'en')).toBe('TRAIL 1.50 · 101.96');
	});

	it('a planned trailing stop becomes the order it names', () => {
		expect(plannedTrailOrder({ mode: 'pct', value: '2' }, 0.01, trailRateFromPercent, parseDecimal)).toEqual({ trailRate: '0.020000' });
		expect(plannedTrailOrder({ mode: 'price', value: '1,5' }, 0.01, trailRateFromPercent, parseDecimal)).toEqual({ trail: '1.50' });
		expect(plannedTrailOrder({ mode: 'price', value: '0' }, 0.01, trailRateFromPercent, parseDecimal)).toBeNull();
	});
});

// The trigger the academy step and the chart compute equals the engine's own trailing order, bar by bar.
describe.skipIf(!wasmBuilt)('trailing trigger against the engine (scripted run)', () => {
	let engine: NodeEngine;
	beforeAll(async () => {
		engine = await loadEngine();
	});

	it('a 2% trailing stop on a long follows the best close exactly', () => {
		const run = engine.createRun('m07-trend-trailing', 3);
		const call = (fn: string, args: (string | number)[], types: ('string' | 'number')[]) => engine.call<{ accepted: boolean }>(fn, args, types);
		expect(call('tl_place_order', [run, 1, 0, 0, '10', '', 0], ['number', 'number', 'number', 'number', 'string', 'string', 'number']).accepted).toBe(true);
		engine.advance(run, 1);
		const placed = (engine.state(run) as GameState).step;
		expect(call('tl_place_trailing_rate', [run, 2, 1, '10', 1, '0.02'], ['number', 'number', 'number', 'string', 'number', 'string']).accepted).toBe(true);
		let compared = 0;
		for (let i = 0; i < 12; i++) {
			const s = engine.advance(run, 1) as GameState;
			const trail = s.orders.find((o) => o.type === 'trailing_stop');
			if (!trail) break; // triggered
			const best = bestClose(s.bars.map((b) => Number(b[3])), placed, 'sell')!;
			const closeText = s.bars[best.step][3];
			expect(trail.price, `bar ${s.step}`).toBe(trailTrigger('sell', closeText, { rate: trail.trailRate }, s.contract.tickSize));
			compared++;
		}
		expect(compared).toBeGreaterThan(3);
	});
});
