import { describe, expect, it } from 'vitest';
import {
	estimate,
	fitToMargin,
	liquidationPrice,
	opposingEffect,
	qtyForEquityPct,
	qtyForNotional,
	qtyForRisk,
	releveredLiquidation,
	requiredMargin,
	sizedQty,
	type Costs
} from './sizing.ts';

const zero: Costs = { multiplier: 1, takerFee: 0, maintenanceMargin: 0.01, halfSpread: 0, slippage: 0 };

describe('sizing', () => {
	it('matches the spec sizing example: 10,000 x 1% / (5 + 1) = 16.6', () => {
		// entry 100, stop 95, extra cost ~1 per unit modelled as fees: use the cost-free helper and add 1 to the distance.
		const k: Costs = { ...zero };
		expect(qtyForRisk(10_000, 1, 100, 94, k, 0.1)).toBeCloseTo(16.6, 6);
	});

	it('rounds risk-based size down to the lot', () => {
		expect(qtyForRisk(10_000, 1, 100, 97, zero, 1)).toBe(33);
	});

	it('liquidation price matches the engine formula (FUT02 numbers)', () => {
		// 10x long, qty 10 @ 100 => margin 100, mmr 1% => (1000 - 100) / (10 * 0.99)
		expect(liquidationPrice('buy', 10, 100, 100, zero)).toBeCloseTo(90.909, 3);
		expect(liquidationPrice('sell', 10, 100, 100, zero)).toBeCloseTo(108.911, 3);
		expect(liquidationPrice('buy', 1, 100, 100, zero)).toBeNull(); // fully collateralized
	});

	it('flags a stop beyond the liquidation price (FUT04)', () => {
		const e = estimate({ side: 'buy', qty: 10, entry: 100, leverage: 10, stop: 88 }, zero);
		expect(e.stopBeyondLiquidation).toBe(true);
		const ok = estimate({ side: 'buy', qty: 10, entry: 100, leverage: 10, stop: 95 }, zero);
		expect(ok.stopBeyondLiquidation).toBe(false);
		expect(ok.plannedRisk).toBeCloseTo(50, 6);
	});
});

describe('changing leverage on an open position', () => {
	it('re-margins the whole position: matches the engine (51 long @ 100.34, 10x -> 20x, MMR 0.5%)', () => {
		const k: Costs = { multiplier: 1, takerFee: 0.0005, maintenanceMargin: 0.005, halfSpread: 0, slippage: 0 };
		expect(releveredLiquidation('buy', 51, 100.34, 20, k)).toBeCloseTo(95.8, 1); // engine showed 95.80
		expect(releveredLiquidation('buy', 51, 100.34, 10, k)).toBeCloseTo(90.75, 1); // 10x: 90.76 in the engine
		expect(releveredLiquidation('sell', -51, 100.34, 10, k)!).toBeGreaterThan(100.34);
	});
});

describe('fitting an order to the available balance', () => {
	const k: Costs = { multiplier: 1, takerFee: 0.0005, maintenanceMargin: 0.005, halfSpread: 0, slippage: 0 };

	it('requires initial margin plus the taker fee, like the engine on placement', () => {
		expect(requiredMargin(100, 100, 1, k)).toBeCloseTo(10_005, 6);
		expect(requiredMargin(100, 100, 2, k)).toBeCloseTo(5_005, 6);
	});

	it('regression: a 1% risk size that needs more than 1x raises the leverage instead of failing', () => {
		// Before: m03 seed 3 applied 120.4 at 1x (margin 12,306 vs 10,000 available) and the engine refused it.
		expect(fitToMargin(120.4, 102, 10_000, 1, 20, k, 0.1)).toEqual({ qty: 120.4, leverage: 2 });
		expect(fitToMargin(41.8, 100, 10_000, 1, 5, k, 0.1)).toEqual({ qty: 41.8, leverage: 1 }); // fits: unchanged
		expect(fitToMargin(41.8, 100, 10_000, 3, 5, k, 0.1)).toEqual({ qty: 41.8, leverage: 3 }); // never lowers it
	});

	it('cuts the size to what fits at the cap when even the cap is not enough', () => {
		const fit = fitToMargin(1000, 100, 10_000, 1, 5, k, 0.1);
		expect(fit.leverage).toBe(5);
		expect(fit.qty).toBeLessThan(500);
		expect(requiredMargin(fit.qty, 100, 5, k)).toBeLessThanOrEqual(10_000);
		expect(requiredMargin(fit.qty + 0.1, 100, 5, k)).toBeGreaterThan(10_000);
	});
});

describe('sizing by notional and by % of equity', () => {
	const k: Costs = { multiplier: 1, takerFee: 0.0005, maintenanceMargin: 0.005, halfSpread: 0, slippage: 0 };
	const x = { equity: 10_000, entry: 103, stop: null, qty: 7, amount: 0 };

	it('a notional buys what it covers at the entry, rounded down to the lot', () => {
		expect(qtyForNotional(1_000, 103, k, 0.1)).toBeCloseTo(9.7, 9); // 1,000 / 103 = 9.708
		expect(qtyForNotional(1_000, 103, { ...k, multiplier: 10 }, 0.1)).toBeCloseTo(0.9, 9);
		expect(qtyForNotional(0, 103, k, 0.1)).toBe(0);
		expect(qtyForNotional(Number.NaN, 103, k, 0.1)).toBe(0);
	});

	it('% of equity is exposure: 100% is one times the equity, 250% needs leverage', () => {
		expect(qtyForEquityPct(10_000, 10, 100, k, 0.1)).toBeCloseTo(10, 9);
		expect(qtyForEquityPct(10_000, 250, 100, k, 1)).toBe(250);
	});

	it('sizedQty picks the mode; risk without a stop keeps the quantity field', () => {
		expect(sizedQty('qty', { ...x, qty: 7.25 }, k, 0.1)).toBeCloseTo(7.2, 9);
		expect(sizedQty('notional', { ...x, amount: 1_030 }, k, 0.1)).toBeCloseTo(10, 9);
		expect(sizedQty('equity', { ...x, amount: 20.6 }, k, 1)).toBe(20);
		expect(sizedQty('risk', x, k, 0.1)).toBe(7);
		expect(sizedQty('risk', { ...x, stop: 100, amount: 1 }, k, 0.1)).toBe(qtyForRisk(10_000, 1, 103, 100, k, 0.1));
	});
});

describe('opposingEffect', () => {
	it('regression: a risk-sized sell against an open long is flagged as reducing, closing or reversing it', () => {
		// Before: a 52.3 sell against a 60.0 long said nothing, though it mostly closed the long.
		expect(opposingEffect(60, 'sell', 52.3, 0.1)).toEqual({ kind: 'reduce', open: 60, after: expect.closeTo(7.7, 9) });
		expect(opposingEffect(60, 'sell', 60, 0.1)).toEqual({ kind: 'close', open: 60, after: 0 });
		expect(opposingEffect(-10, 'buy', 25.5, 0.1)).toEqual({ kind: 'reverse', open: 10, after: expect.closeTo(15.5, 9) });
	});
	it('says nothing for a flat account or an order on the same side', () => {
		expect(opposingEffect(0, 'sell', 10, 0.1)).toBeNull();
		expect(opposingEffect(5, 'buy', 10, 0.1)).toBeNull();
		expect(opposingEffect(-5, 'sell', 10, 0.1)).toBeNull();
		expect(opposingEffect(5, 'sell', 0, 0.1)).toBeNull();
	});
});
