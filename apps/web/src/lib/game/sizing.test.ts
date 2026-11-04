import { describe, expect, it } from 'vitest';
import { estimate, liquidationPrice, marginShortfall, qtyForRisk, releveredLiquidation, type Costs } from './sizing.ts';

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

	it('regression: flags an order whose margin + fee exceeds the available balance and suggests the lowest leverage', () => {
		// 136 contracts at 103.28 = 14,046 notional: at 1x it needs 14,046 + 7.02 fee > 10,000; 2x fits.
		const k: Costs = { ...zero, takerFee: 0.0005 };
		const short = marginShortfall(136, 103.28, 1, 10_000, 5, k);
		expect(short?.need).toBeCloseTo(14_046.08 + 7.02304, 2);
		expect(short?.minLeverage).toBe(2);
		expect(marginShortfall(136, 103.28, 2, 10_000, 5, k)).toBeNull();
		expect(marginShortfall(600, 103.28, 1, 10_000, 5, k)?.minLeverage).toBeNull(); // 62k notional: even 5x is too little
		expect(marginShortfall(0, 103.28, 1, 10_000, 5, k)).toBeNull(); // nothing opens (e.g. a pure close)
	});
});
