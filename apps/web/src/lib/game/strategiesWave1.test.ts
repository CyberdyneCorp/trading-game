// Builder wave 1 (add-practice-and-advanced-strategies D2, D3, D9): limit legs and the default limit,
// legs on two expiries, calendar and diagonal templates, roll orders, the hedge to neutral, combo
// submissions (opening, rolls, adjustments) and their states.
import { describe, expect, it } from 'vitest';
import type { GameState, OptionQuoteView } from '#lib/engine/types.ts';
import { ev, game } from './fixtures.test-util.ts';
import { comboHoldings, comboStatus, combosFromLog, groupTrades, nextSubmissionNumber, workingLegs, type LoggedAction } from './combos.ts';
import {
	ONE_EXPIRY_TEMPLATES,
	TEMPLATES,
	templateLegs,
	comboLegs,
	defaultLimit,
	hedgeOrder,
	legExpiries,
	legsProblem,
	orderLegs,
	marginLabelKey,
	rollOrders,
	rollTargets,
	strategyOf,
	twoExpiryTemplateLegs,
	whatifLegs,
	type BuilderLeg
} from './strategies.ts';

const STRIKES = [95, 97.5, 100, 102.5, 105];
const NEAR = 80;
const FAR = 120;
const LATER = 160;

function quote(id: number, kind: 'call' | 'put', strike: number, expiryStep: number, mid = '2.345'): OptionQuoteView {
	return {
		id, kind, strike: String(strike), listStep: 0, expiryStep, listed: true, bid: '2.2', ask: '2.5', mid, iv: '0.5',
		delta: '0.5', gamma: '0.01', theta: '-0.01', vega: '0.1', ivFromRealized: false, settling: false
	};
}

/** Series ids: expiry index x 100 + strike index x 2 + 1 (call) or + 2 (put). */
function chain(): OptionQuoteView[] {
	return [NEAR, FAR, LATER].flatMap((e, ei) => STRIKES.flatMap((k, i) => [quote(ei * 100 + 2 * i + 1, 'call', k, e), quote(ei * 100 + 2 * i + 2, 'put', k, e)]));
}

const id = (expiry: number, kind: 'call' | 'put', strike: number) => [NEAR, FAR, LATER].indexOf(expiry) * 100 + 2 * STRIKES.indexOf(strike) + (kind === 'call' ? 1 : 2);

function g(extra: Partial<GameState> = {}): GameState {
	const base = game([]);
	return {
		...base,
		step: 60,
		contract: { lotSize: '0.1', multiplier: '1', tickSize: '0.01' } as GameState['contract'],
		options: { ...base.options, enabled: true, allowShort: true, multiplier: '1', premiumTick: '0.01', lotSize: '0.1', chain: chain() },
		...extra
	};
}

describe('calendar and diagonal templates (design D3)', () => {
	const near = { step: NEAR, strikes: STRIKES };
	const far = { step: FAR, strikes: STRIKES };

	it('a calendar sells the near ATM and buys the far ATM; a diagonal sells the next strike out and buys the ATM', () => {
		const show = (legs: BuilderLeg[] | null) => legs!.map((l) => `${l.side} ${l.kind} ${l.strike} @${l.expiry}`);
		expect(show(twoExpiryTemplateLegs('calendar_call', near, far, 101, 1))).toEqual(['sell call 100 @80', 'buy call 100 @120']);
		expect(show(twoExpiryTemplateLegs('calendar_put', near, far, 99, 1))).toEqual(['sell put 100 @80', 'buy put 100 @120']);
		// Calls: K1 (bought, far) below K2 (sold, near); puts: K1 above K2.
		expect(show(twoExpiryTemplateLegs('diagonal_call', near, far, 100.4, 1))).toEqual(['sell call 102.5 @80', 'buy call 100 @120']);
		expect(show(twoExpiryTemplateLegs('diagonal_put', near, far, 100.4, 1))).toEqual(['sell put 97.5 @80', 'buy put 100 @120']);
	});

	it('the reverse (short) versions flip every side; they need a later expiry listing the strike', () => {
		const short = twoExpiryTemplateLegs('calendar_call', near, far, 100, 2, true)!;
		expect(short.map((l) => `${l.side} x${l.qty}`)).toEqual(['buy x2', 'sell x2']);
		expect(strategyOf('calendar_call', true)).toBe('short_calendar_call');
		expect(strategyOf('diagonal_put', false)).toBe('diagonal_put');
		expect(twoExpiryTemplateLegs('calendar_call', near, { step: NEAR, strikes: STRIKES }, 100, 1)).toBeNull();
		expect(twoExpiryTemplateLegs('calendar_call', near, { step: FAR, strikes: [90] }, 100, 1)).toBeNull();
		expect(twoExpiryTemplateLegs('iron_condor', near, far, 100, 1)).toBeNull();
	});

	it('legs on up to two expiries; a third is a problem', () => {
		const legs: BuilderLeg[] = [
			{ kind: 'call', side: 'sell', strike: 100, qty: 1, expiry: NEAR },
			{ kind: 'call', side: 'buy', strike: 100, qty: 1, expiry: FAR }
		];
		expect(legsProblem(legs, NEAR)).toBeNull();
		expect(legExpiries(legs, NEAR)).toEqual([NEAR, FAR]);
		expect(legsProblem([...legs, { kind: 'put', side: 'buy', strike: 95, qty: 1, expiry: LATER }], NEAR)).toBe('expiries');
		const orders = orderLegs(g(), NEAR, legs)!;
		expect(orders.map((o) => o.series)).toEqual([id(FAR, 'call', 100), id(NEAR, 'call', 100)]); // buys first
	});
});

describe('the Academy builder (one expiry)', () => {
	it('lists only templates that build legs on a single expiry (playtest: its calendars built none)', () => {
		expect(ONE_EXPIRY_TEMPLATES).toHaveLength(TEMPLATES.length - 4);
		for (const id of ONE_EXPIRY_TEMPLATES) expect(templateLegs(id, [90, 95, 97.5, 100, 102.5, 105, 110], 100, 1), id).not.toBeNull();
	});
});

describe('limit legs (design D2)', () => {
	it('the default limit is the mid rounded to the tick on the passive side', () => {
		expect(defaultLimit({ mid: '2.345' }, 'buy', 0.01)).toBeCloseTo(2.34, 10);
		expect(defaultLimit({ mid: '2.345' }, 'sell', 0.01)).toBeCloseTo(2.35, 10);
		expect(defaultLimit({ mid: '2.34' }, 'sell', 0.01)).toBeCloseTo(2.34, 10);
		expect(defaultLimit({ mid: '0.001' }, 'buy', 0.01)).toBeCloseTo(0.01, 10); // never below one tick
	});

	it('a limit leg sends its premium; the dry run gets the combo time in force; the analysis values it at the limit', () => {
		const legs: BuilderLeg[] = [
			{ kind: 'call', side: 'buy', strike: 100, qty: 1, expiry: NEAR, limit: 2.34 },
			{ kind: 'call', side: 'sell', strike: 102.5, qty: 1, expiry: NEAR }
		];
		const orders = orderLegs(g(), NEAR, legs)!;
		expect(orders[0]).toEqual({ series: id(NEAR, 'call', 100), side: 'buy', qty: '1.0', premium: '2.34' });
		expect(orders[1].premium).toBeUndefined();
		expect(comboLegs(orders, { tif: 'ioc' })).toEqual([
			{ series: id(NEAR, 'call', 100), side: 'buy', qty: '1.0', type: 'limit', premium: '2.34', tif: 'ioc' },
			{ series: id(NEAR, 'call', 102.5), side: 'sell', qty: '1.0' }
		]);
		expect(comboLegs(orders, { tif: 'bars', tifBars: 3 })[0]).toMatchObject({ tif: 'bars', tifBars: 3 });
		expect(comboLegs(orders, { tif: 'gtc' })[0]).not.toHaveProperty('tif');
		expect(whatifLegs(orders, null)[0]).toMatchObject({ entry: '2.34' });
		expect(whatifLegs(orders, null)[1]).not.toHaveProperty('entry');
	});
});

describe('roll (design D3)', () => {
	it('closes the near legs and opens the same strikes on the later expiry, buys first', () => {
		const held = [
			{ series: id(NEAR, 'call', 100), qty: -1 },
			{ series: id(FAR, 'call', 100), qty: 1 }
		];
		const plan = rollOrders(g(), held, LATER, false, 101)!;
		expect(plan.from).toBe(NEAR);
		expect(plan.orders).toEqual([
			{ series: id(NEAR, 'call', 100), side: 'buy', qty: '1.0' }, // buy back the near short
			{ series: id(LATER, 'call', 100), side: 'sell', qty: '1.0' } // write the same strike later
		]);
		expect(rollTargets(chain(), NEAR)).toEqual([FAR, LATER]);
		// The combo holds FAR: never onto it (playtest: rolling onto FAR netted the long and closed the calendar).
		expect(rollTargets(chain(), NEAR, [NEAR, FAR])).toEqual([LATER]);
		expect(rollOrders(g(), held, FAR, false, 101)).toBeNull();
	});

	it('re-centred at the money: every strike shifts by the ATM minus the structure middle', () => {
		const held = [
			{ series: id(NEAR, 'put', 95), qty: 1 },
			{ series: id(NEAR, 'put', 97.5), qty: -1 }
		];
		const plan = rollOrders(g(), held, FAR, true, 102)!;
		// Middle 96.25, ATM 102.5: +6.25, so 95 -> 101.25 -> nearest 100 and 97.5 -> 103.75 -> nearest 102.5 (ties keep the lower).
		expect(plan.orders.map((o) => `${o.side} ${o.series}`)).toEqual([
			`buy ${id(FAR, 'put', 100)}`,
			`buy ${id(NEAR, 'put', 97.5)}`,
			`sell ${id(NEAR, 'put', 95)}`,
			`sell ${id(FAR, 'put', 102.5)}`
		]);
		expect(rollOrders(g(), held, NEAR, false, 100)).toBeNull(); // not later
		expect(rollOrders(g(), [], FAR, false, 100)).toBeNull();
	});
});

describe('margin label (design D5)', () => {
	it('a two-expiry structure without the calendar margin is not called a covered pair (playtest, m17)', () => {
		expect(marginLabelKey({ spreadMargin: true }, false)).toBe('builder.marginSpread');
		expect(marginLabelKey({ spreadMargin: true }, true)).toBe('builder.marginSameExpiry');
		expect(marginLabelKey({ spreadMargin: true, calendarMargin: true }, true)).toBe('builder.marginSpread');
		expect(marginLabelKey({}, true)).toBe('builder.marginPerLeg');
	});
});

describe('hedge to neutral (design D9)', () => {
	it('-deltaUnits in perpetual contracts, rounded toward zero to the lot', () => {
		const game1 = g();
		expect(hedgeOrder(game1, 6.37)).toEqual({ series: 0, side: 'sell', qty: '6.3' });
		expect(hedgeOrder(game1, -2.99)).toEqual({ series: 0, side: 'buy', qty: '2.9' });
		expect(hedgeOrder(game1, 0.05)).toBeNull();
		const twoPerContract = g({ contract: { lotSize: '1', multiplier: '2', tickSize: '0.01' } as GameState['contract'] });
		expect(hedgeOrder(twoPerContract, 7)).toEqual({ series: 0, side: 'sell', qty: '3' }); // 3.5 contracts -> 3
	});
});

describe('combo submissions: opening, rolls and adjustments keep one combo (design D3)', () => {
	const log: LoggedAction[] = [
		{ k: 'opt', series: 101, side: 'buy', qty: '1', combo: 'c1', strategy: 'calendar_call', type: 'limit', premium: '4.10' },
		{ k: 'opt', series: 1, side: 'sell', qty: '1', combo: 'c1', strategy: 'calendar_call' },
		{ k: 'adv', n: 2 },
		{ k: 'opt', series: 1, side: 'buy', qty: '1', combo: 'c1', strategy: 'calendar_call', roll: '1' },
		{ k: 'opt', series: 201, side: 'sell', qty: '1', combo: 'c1', strategy: 'calendar_call', roll: '1' },
		{ k: 'adv', n: 1 },
		{ k: 'order', side: 'sell', qty: '0.4', combo: 'c1', strategy: 'calendar_call', adjust: '1' }
	];

	it('groups the submissions under one record, each at its step', () => {
		const [rec] = combosFromLog(log, 60);
		expect(rec.legs).toHaveLength(2);
		expect(rec.legs[0].premium).toBe('4.10');
		expect(rec.submissions.map((s) => `${s.kind}${s.n}@${s.step}:${s.legs.length}`)).toEqual(['open@60:2', 'roll1@62:2', 'adjust1@63:1']);
		expect(nextSubmissionNumber(rec, 'roll')).toBe('2');
		expect(nextSubmissionNumber(rec, 'adjust')).toBe('2');
	});

	it('states follow the latest submission; holdings net every filled leg', () => {
		const [rec] = combosFromLog(log, 60);
		const events = [
			ev({ type: 'order', step: 60, orderId: 1, status: 'accepted', side: 'buy', qty: '1' }),
			ev({ type: 'order', step: 60, orderId: 2, status: 'accepted', side: 'sell', qty: '1' }),
			ev({ type: 'fill', step: 61, orderId: 1, side: 'buy', qty: '1', price: '4.10', series: 101 }),
			ev({ type: 'fill', step: 61, orderId: 2, side: 'sell', qty: '1', price: '2.00', series: 1 }),
			ev({ type: 'order', step: 62, orderId: 3, status: 'accepted', side: 'buy', qty: '1' }),
			ev({ type: 'order', step: 62, orderId: 4, status: 'accepted', side: 'sell', qty: '1' }),
			ev({ type: 'fill', step: 63, orderId: 3, side: 'buy', qty: '1', price: '1.00', series: 1 }),
			ev({ type: 'order', step: 63, orderId: 5, status: 'accepted', side: 'sell', qty: '0.4' })
		];
		const st = comboStatus(events, rec);
		expect(st.state).toBe('working'); // the adjustment is still working
		expect(st.allLegs.map((l) => l.state)).toEqual(['filled', 'filled', 'filled', 'working', 'working']);
		expect(workingLegs(st)).toEqual([5]);
		expect(comboHoldings(st)).toEqual([{ series: 101, qty: 1 }]); // the near short bought back; 201 still working
		// The debrief keeps one row for the whole combo.
		const trade = (n: number, series: number, side: 'long' | 'short', entryStep: number) =>
			({ n, series, side, entryStep, net: '1', rBp: null }) as unknown as import('#lib/engine/types.ts').EngineTrade;
		const grouped = groupTrades([trade(1, 101, 'long', 61), trade(2, 1, 'short', 61), trade(3, 7, 'long', 61)], [st]);
		expect(grouped.combos).toHaveLength(1);
		expect(grouped.combos[0].trades.map((t) => t.n)).toEqual([1, 2]);
		expect(grouped.rest.map((t) => t.n)).toEqual([3]);
	});

	it('an opening still working: complete only when every leg filled; void when none did', () => {
		const [rec] = combosFromLog(log.slice(0, 2), 60);
		const accepted = [
			ev({ type: 'order', step: 60, orderId: 1, status: 'accepted', side: 'buy', qty: '1' }),
			ev({ type: 'order', step: 60, orderId: 2, status: 'accepted', side: 'sell', qty: '1' })
		];
		expect(comboStatus(accepted, rec).state).toBe('working');
		const ioc = [...accepted, ...[1, 2].map((orderId) => ev({ type: 'order', step: 61, orderId, status: 'cancelled', reason: 'time_in_force' }))];
		expect(comboStatus(ioc, rec).state).toBe('not_executed');
	});
});
