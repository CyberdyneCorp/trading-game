import { describe, expect, it } from 'vitest';
import type { OptionQuoteView } from '#lib/engine/types.ts';
import { game } from './fixtures.test-util.ts';
import {
	TEMPLATES,
	expiriesOf,
	legsProblem,
	orderLegs,
	positionLegs,
	COVERS_LONG,
	contractsForRisk,
	coverContracts,
	heldLong,
	longStop,
	sameExpiry,
	strategyOf,
	strikesOf,
	submissionOrder,
	templateLegs,
	templateWrites,
	whatifLegs,
	type BuilderLeg
} from './strategies.ts';

// Strikes 2,5 apart around 100 (strategy-builder spec "Iron condor from the chain").
const STRIKES = [90, 92.5, 95, 97.5, 100, 102.5, 105, 107.5, 110];

function quote(id: number, kind: 'call' | 'put', strike: number, expiryStep = 50, extra: Partial<OptionQuoteView> = {}): OptionQuoteView {
	return {
		id, kind, strike: String(strike), listStep: 0, expiryStep, listed: true, bid: '1', ask: '1.1', mid: '1.05', iv: '0.5',
		delta: '0.5', gamma: '0.01', theta: '-0.01', vega: '0.1', ivFromRealized: false, settling: false, ...extra
	};
}

function chain(): OptionQuoteView[] {
	return STRIKES.flatMap((k, i) => [quote(2 * i + 1, 'call', k), quote(2 * i + 2, 'put', k)]);
}

describe('templates from the chain', () => {
	it('iron condor at 100 with strikes 2,5 apart: long put, short put, short call, long call with K1 < K2 < 100 < K3 < K4', () => {
		const legs = templateLegs('iron_condor', STRIKES, 100, 1)!;
		expect(legs.map((l) => [l.kind, l.side, l.strike])).toEqual([
			['put', 'buy', 95],
			['put', 'sell', 97.5],
			['call', 'sell', 102.5],
			['call', 'buy', 105]
		]);
	});

	it('the other templates pick strikes around the forward', () => {
		const f = 101;
		const pick = (id: (typeof TEMPLATES)[number]) => templateLegs(id, STRIKES, f, 2)!.map((l) => `${l.side} ${l.kind} ${l.strike ?? '-'} x${l.qty}`);
		expect(pick('bull_call')).toEqual(['buy call 100 x2', 'sell call 102.5 x2']);
		expect(pick('bear_put')).toEqual(['buy put 100 x2', 'sell put 97.5 x2']);
		expect(pick('covered_call')).toEqual(['buy perp - x2', 'sell call 102.5 x2']);
		expect(pick('protective_put')).toEqual(['buy perp - x2', 'buy put 100 x2']);
		expect(pick('straddle')).toEqual(['buy call 100 x2', 'buy put 100 x2']);
		expect(pick('strangle')).toEqual(['buy call 102.5 x2', 'buy put 100 x2']);
	});

	it('the short toggle flips straddle and strangle only; a chain without the strikes gives no legs', () => {
		expect(templateLegs('straddle', STRIKES, 100, 1, true)!.every((l) => l.side === 'sell')).toBe(true);
		expect(templateLegs('bull_call', STRIKES, 100, 1, true)![0].side).toBe('buy');
		expect(strategyOf('strangle', true)).toBe('short_strangle');
		expect(strategyOf('iron_condor', true)).toBe('iron_condor');
		expect(templateLegs('iron_condor', [100, 105], 101, 1)).toBeNull();
		expect(templateLegs('straddle', [], 100, 1)).toBeNull();
	});

	it('writing locked: spreads, covered call, condor and the short versions need writing; protective put, straddle and strangle do not', () => {
		const writes = TEMPLATES.filter((id) => templateWrites(id, false));
		expect(writes).toEqual(['bull_call', 'bear_put', 'covered_call', 'iron_condor', 'calendar_call', 'calendar_put', 'diagonal_call', 'diagonal_put']);
		expect(templateWrites('straddle', true)).toBe(true);
		expect(templateWrites('strangle', true)).toBe(true);
		expect(templateWrites('protective_put', true)).toBe(false);
	});
});

describe('submission order and leg checks', () => {
	it('long options first, then the perpetual, then short options (stable within each group)', () => {
		const legs: BuilderLeg[] = [
			{ kind: 'call', side: 'sell', strike: 105, qty: 1 },
			{ kind: 'perp', side: 'buy', strike: null, qty: 1 },
			{ kind: 'put', side: 'buy', strike: 90, qty: 1 },
			{ kind: 'call', side: 'buy', strike: 110, qty: 1 },
			{ kind: 'put', side: 'sell', strike: 95, qty: 1 }
		];
		expect(submissionOrder(legs).map((l) => `${l.side} ${l.kind} ${l.strike ?? ''}`)).toEqual([
			'buy put 90',
			'buy call 110',
			'buy perp ',
			'sell call 105',
			'sell put 95'
		]);
	});

	it('at most four option legs and one perpetual; quantities above zero; one expiry', () => {
		const c = (strike: number): BuilderLeg => ({ kind: 'call', side: 'buy', strike, qty: 1 });
		const p: BuilderLeg = { kind: 'perp', side: 'buy', strike: null, qty: 1 };
		expect(legsProblem([])).toBe('empty');
		expect(legsProblem([c(1), c(2), c(3), c(4), c(5)])).toBe('too_many');
		expect(legsProblem([c(1), p, p])).toBe('two_perps');
		expect(legsProblem([{ ...c(1), qty: 0 }])).toBe('qty');
		expect(legsProblem([c(1), c(2), c(3), c(4), p])).toBeNull();
		expect(sameExpiry([quote(1, 'call', 100, 50), quote(2, 'put', 100, 50)])).toBe(true);
		expect(sameExpiry([quote(1, 'call', 100, 50), quote(2, 'put', 100, 90)])).toBe(false);
		expect(sameExpiry([quote(1, 'call', 100, 50), undefined])).toBe(false);
	});

	it('orders name series ids in submission order, the perpetual by contracts; an unlisted strike gives none', () => {
		const g = game([], {
			contract: { lotSize: '0.1', multiplier: '1' } as never,
			options: { ...game([]).options, chain: chain(), lotSize: '0.1' }
		});
		const legs = templateLegs('covered_call', STRIKES, 100, 3)!;
		expect(orderLegs(g, 50, legs)).toEqual([
			{ series: 0, side: 'buy', qty: '3.0' },
			{ series: 11, side: 'sell', qty: '3.0' }
		]);
		expect(orderLegs(g, 50, [{ kind: 'call', side: 'buy', strike: 101, qty: 1 }])).toBeNull();
		expect(whatifLegs([{ series: 13, side: 'sell', qty: '1' }], { qty: '2', entry: '99' })).toEqual([
			{ series: 13, side: 'sell', qty: '1' },
			{ kind: 'perp', side: 'buy', qty: '2', entry: '99' }
		]);
	});

	it('expiries skip settling series; strikes sorted; positions become what-if legs at their entries', () => {
		const ch = [...chain(), quote(99, 'call', 100, 20, { settling: true })];
		expect(expiriesOf(ch)).toEqual([50]);
		expect(strikesOf(ch, 50)).toEqual(STRIKES);
		const g = game([], {
			position: { qty: '-2', entry: '101', margin: '10' },
			options: { ...game([]).options, positions: [{ series: 5, qty: '-1', avgPremium: '2.5' }] }
		});
		expect(positionLegs(g)).toEqual([
			{ series: 5, side: 'sell', qty: '1', entry: '2.5' },
			{ kind: 'perp', side: 'sell', qty: '2', entry: '101' }
		]);
	});

	it('Regression: a covered call or protective put on a held long covers all of it (48.2 units, not 1 contract)', () => {
		const g = game([], {
			contract: { ...game([]).contract, multiplier: '1' },
			options: { ...game([]).options, multiplier: '1', lotSize: '0.1' }
		});
		expect(coverContracts(g, { qty: '48.20000000' })).toBe(48.2);
		expect(coverContracts(g, { qty: '48.27' })).toBe(48.2); // down to the option lot, never more than held
		expect([...COVERS_LONG].sort()).toEqual(['covered_call', 'protective_put']);
	});
});

describe('Regression: a combo on a held long plans the risk to the long\'s stop', () => {
	const stop = (id: number, price: string, qty: string, extra: Record<string, unknown> = {}) => ({
		id, type: 'stop_market', side: 'sell', qty, filled: '0', price, reduceOnly: true, status: 'accepted', placedStep: 60,
		parentId: 0, ocoGroup: 0, trailDistance: '0', bracketStop: '0', bracketTp: '0', series: 0, ...extra
	});

	it('the held long carries the highest working stop that closes all of it, and the what-if leg passes it on', () => {
		const g = game([], {
			position: { qty: '48', entry: '100', margin: '1600' },
			orders: [stop(1, '96', '48'), stop(2, '97', '48'), stop(3, '99', '10'), stop(4, '98', '48', { reduceOnly: false })] as never
		});
		expect(longStop(g)).toBe('97');
		expect(heldLong(g)).toEqual({ qty: '48', entry: '100', stop: '97' });
		expect(whatifLegs([{ series: 13, side: 'sell', qty: '48' }], heldLong(g))).toEqual([
			{ series: 13, side: 'sell', qty: '48' },
			{ kind: 'perp', side: 'buy', qty: '48', entry: '100', stop: '97' }
		]);
	});

	it('without a stop covering the long (none, partial, an option order) the leg has none', () => {
		const partial = game([], { position: { qty: '48', entry: '100', margin: '1600' }, orders: [stop(1, '97', '20'), stop(2, '97', '48', { series: 7 })] as never });
		expect(longStop(partial)).toBeNull();
		expect(heldLong(partial)).toEqual({ qty: '48', entry: '100' });
		expect(heldLong(game([], { position: { qty: '-3', entry: '100', margin: '100' } }))).toBeNull();
	});
});

describe('builder sizing aid: contracts from the maximum loss', () => {
	it('scales the engine\'s maximum loss at n contracts to the risk budget, down to the lot', () => {
		// A condor losing 2 per contract at most: 1% of 10,000 = 100 -> 50 contracts.
		expect(contractsForRisk(10_000, 1, 2, 1, 0.1)).toBe(50);
		// The same condor analysed at 3 contracts (max loss 6): the per-contract loss is what counts.
		expect(contractsForRisk(10_000, 1, 6, 3, 0.1)).toBe(50);
		expect(contractsForRisk(10_000, 2, 3.3, 1, 0.1)).toBe(60.6);
	});

	it('no answer without a bounded, positive maximum loss or a usable budget', () => {
		expect(contractsForRisk(10_000, 1, null, 1, 0.1)).toBeNull();
		expect(contractsForRisk(10_000, 1, 0, 1, 0.1)).toBeNull();
		expect(contractsForRisk(10_000, 0, 2, 1, 0.1)).toBeNull();
		expect(contractsForRisk(10_000, 1, 2, 0, 0.1)).toBeNull();
	});
});
