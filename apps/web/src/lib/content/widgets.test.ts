import { describe, expect, it } from 'vitest';
import type { PayoffLeg, TicketWidget } from './academy.ts';
import { FIRST_TRADE, TERMINAL_STEPS } from './coach.ts';
import { breakevens, maxLoss, maxProfit, payoffCurve, pnlAt } from './payoff.ts';
import { initialInputs, reachesTarget, sandboxOutputs, sandboxValue } from './ticketSandbox.ts';

const ticket: TicketWidget = {
	kind: 'ticket',
	market: 'R',
	equity: 10_000,
	entry: 100,
	side: 'buy',
	qty: 10,
	leverage: 5,
	maxLeverage: 25,
	stop: 96,
	target: 108,
	mmr: 0.01,
	lot: 1,
	edit: ['qty', 'leverage']
};

describe('ticket sandbox view-model', () => {
	it('Sizing by moving the ticket: 25 units with entry 100 and stop 96 risk 1% of 10,000', () => {
		const x = { ...initialInputs(ticket), qty: 25 };
		const out = sandboxOutputs(ticket, x);
		expect(out.riskAtStop).toBeCloseTo(100, 9);
		expect(out.riskPct).toBeCloseTo(1, 9);
		expect(reachesTarget(sandboxValue('riskAtStop', x, out), 100, 4)).toBe(true);
		expect(reachesTarget(sandboxValue('riskAtStop', { ...x, qty: 23 }, sandboxOutputs(ticket, { ...x, qty: 23 })), 100, 4)).toBe(false);
	});

	it('leverage changes margin and liquidation, never the risk at the stop', () => {
		const at = (leverage: number) => sandboxOutputs(ticket, { ...initialInputs(ticket), qty: 25, leverage });
		expect(at(5).margin).toBeCloseTo(500, 9);
		expect(at(10).margin).toBeCloseTo(250, 9);
		expect(at(5).riskAtStop).toBeCloseTo(at(10).riskAtStop!, 9);
		expect(at(20).liquidation).toBeCloseTo(95.9596, 3);
		expect(at(20).stopBeyondLiquidation).toBe(false);
		expect(at(21).stopBeyondLiquidation).toBe(true);
		expect(at(5).rTarget).toBe(2);
	});

	it('clamps leverage to the widget cap and reads inputs as set-question fields', () => {
		const x = { ...initialInputs(ticket), leverage: 99 };
		expect(sandboxOutputs(ticket, x).margin).toBeCloseTo(1000 / 25, 9);
		expect(sandboxValue('leverage', x, sandboxOutputs(ticket, x))).toBe(99);
		expect(sandboxValue('side', { ...x, side: 'sell' }, sandboxOutputs(ticket, x))).toBe(-1);
		expect(sandboxOutputs(ticket, { ...x, qty: 0 }).liquidation).toBeNull();
	});
});

describe('expiry payoff analytics', () => {
	const call = (strike: number, price: number, side: 'buy' | 'sell' = 'buy'): PayoffLeg => ({ kind: 'call', side, qty: 1, strike, price });
	const put = (strike: number, price: number, side: 'buy' | 'sell' = 'buy'): PayoffLeg => ({ kind: 'put', side, qty: 1, strike, price });
	const perp = (price: number, side: 'buy' | 'sell' = 'buy'): PayoffLeg => ({ kind: 'perp', side, qty: 1, price });

	it('a bought call: breakeven strike + premium, loss bounded by the premium, gain unbounded', () => {
		expect(breakevens([call(100, 4)])).toEqual([104]);
		expect(pnlAt([call(100, 4)], 110)).toBe(6);
		expect(maxLoss([call(100, 4)])).toBe(4);
		expect(maxProfit([call(100, 4)])).toBeNull();
	});

	it('a short put and a long perpetual have finite losses because prices stop at 0', () => {
		expect(maxLoss([put(95, 2, 'sell')])).toBe(93);
		expect(maxLoss([perp(100)])).toBe(100);
		expect(maxLoss([perp(100, 'sell')])).toBeNull();
	});

	it('matches a fine-grid brute force for every template shape', () => {
		const shapes: PayoffLeg[][] = [
			[call(100, 5.5), call(110, 1.5, 'sell')],
			[put(100, 5), put(90, 1.5, 'sell')],
			[perp(100), call(105, 2, 'sell')],
			[perp(100), put(95, 2)],
			[call(100, 4), put(100, 4)],
			[call(105, 2), put(95, 2)],
			[put(90, 0.8), put(95, 2.3, 'sell'), call(105, 2.3, 'sell'), call(110, 0.8)],
			[put(95, 2, 'sell'), call(105, 2, 'sell')]
		];
		for (const legs of shapes) {
			const grid = Array.from({ length: 30_001 }, (_, i) => i / 100); // 0 … 300 by 0.01
			const values = grid.map((s) => pnlAt(legs, s));
			if (maxProfit(legs) !== null) expect(maxProfit(legs)).toBeCloseTo(Math.max(...values), 6);
			if (maxLoss(legs) !== null) expect(maxLoss(legs)).toBeCloseTo(Math.max(0, -Math.min(...values)), 6);
			const crossings = grid.slice(1).filter((s, i) => Math.sign(values[i]) !== Math.sign(values[i + 1]) && values[i + 1] !== 0).length;
			expect(breakevens(legs).length, JSON.stringify(legs)).toBe(crossings);
			for (const b of breakevens(legs)) expect(Math.abs(pnlAt(legs, b))).toBeLessThan(1e-9);
		}
	});

	it('draws the curve with the strikes as exact corners', () => {
		const curve = payoffCurve([call(100, 4), put(95, 2)], 80, 120, 7);
		expect(curve.map((p) => p.s)).toContain(95);
		expect(curve.map((p) => p.s)).toContain(100);
		expect(curve[0].s).toBe(80);
		expect(curve.at(-1)!.s).toBe(120);
	});
});

describe('guided first trade steps (design D6)', () => {
	it('points at the chart, ticket, stop, risk line, advance, then the debrief, in both locales', () => {
		expect(FIRST_TRADE.id).toBe('first-trade');
		expect(FIRST_TRADE.steps.map((s) => s.target)).toEqual(['chart', 'ticket', 'stop', 'risk', 'advance', 'debrief']);
		expect(TERMINAL_STEPS).toBe(5);
		expect(FIRST_TRADE.steps.map((s) => s.completes)).toEqual(['next', 'next', 'stop', 'next', 'advance', 'next']);
		for (const s of FIRST_TRADE.steps) for (const l of ['pt-BR', 'en'] as const) expect(s.text[l].title && s.text[l].body).toBeTruthy();
	});

	it('describes controls and process only: no market outcome, no mission id', () => {
		const all = JSON.stringify(FIRST_TRADE);
		expect(all).not.toMatch(/\bm\d{2}\b|vai subir|vai cair|will rise|will fall|crash|rally/i);
	});
});
