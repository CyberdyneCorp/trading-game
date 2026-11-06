import { describe, expect, it } from 'vitest';
import type { CommandResult, EngineTrade, ReasonCode } from '#lib/engine/types.ts';
import { ev } from './fixtures.test-util.ts';
import {
	comboOrderIds,
	comboStatus,
	combosFromLog,
	groupTrades,
	netPremium,
	nextComboId,
	openLegs,
	submitCombo,
	type ComboPort,
	type ComboRecord,
	type ComboTag
} from './combos.ts';
import type { OrderLeg } from './strategies.ts';

const TAG: ComboTag = { combo: 'c1', strategy: 'iron_condor' };
const CONDOR: OrderLeg[] = [
	{ series: 1, side: 'buy', qty: '1' },
	{ series: 7, side: 'buy', qty: '1' },
	{ series: 3, side: 'sell', qty: '1' },
	{ series: 5, side: 'sell', qty: '1' }
];

/** A fake engine port: refuses the legs named in `refuse` with `reason`, logs every call. */
function fakePort(refuse: Record<number, ReasonCode> = {}) {
	const calls: string[] = [];
	let next = 10;
	let placed = 0;
	const port: ComboPort = {
		async place(leg, tag) {
			const i = placed++;
			calls.push(`place ${leg.side} ${leg.series} ${tag.combo}`);
			const r = refuse[i];
			return (r ? { accepted: false, reason: r, orderId: 0, warning: null } : { accepted: true, reason: 'none', orderId: next++, warning: null }) as CommandResult;
		},
		async cancel(id, tag) {
			calls.push(`cancel ${id} ${tag.combo}`);
			return { accepted: true, reason: 'none', orderId: id, warning: null } as CommandResult;
		}
	};
	return { port, calls };
}

describe('combo submission', () => {
	it('places every leg in order with the combo tag', async () => {
		const { port, calls } = fakePort();
		expect(await submitCombo(port, CONDOR, TAG)).toEqual({ ok: true, orderIds: [10, 11, 12, 13] });
		expect(calls).toEqual(['place buy 1 c1', 'place buy 7 c1', 'place sell 3 c1', 'place sell 5 c1']);
	});

	it('a refused third leg cancels the two legs already accepted, at once, and stops', async () => {
		const { port, calls } = fakePort({ 2: 'insufficient_margin' });
		const out = await submitCombo(port, CONDOR, TAG);
		expect(out).toEqual({ ok: false, refusedLeg: 2, reason: 'insufficient_margin', cancelled: [10, 11] });
		expect(calls).toEqual(['place buy 1 c1', 'place buy 7 c1', 'place sell 3 c1', 'cancel 10 c1', 'cancel 11 c1']);
	});

	it('a busy session (no result) also withdraws the combo', async () => {
		const port: ComboPort = { place: async () => null, cancel: async () => null };
		expect(await submitCombo(port, CONDOR, TAG)).toEqual({ ok: false, refusedLeg: 0, reason: 'busy', cancelled: [] });
	});

	it('combo ids count up from the log', () => {
		expect(nextComboId([])).toBe('c1');
		expect(nextComboId([{ id: 'c1' }, { id: 'c4' }])).toBe('c5');
	});
});

describe('combos from the action log', () => {
	it('groups tagged legs at the step they were placed; a tagged cancel marks the combo withdrawn', () => {
		const log = [
			{ k: 'adv', n: 3 },
			{ k: 'plan', side: 'buy' as const },
			{ k: 'opt', series: 1, side: 'buy' as const, qty: '1', combo: 'c1', strategy: 'bull_call' as const },
			{ k: 'opt', series: 3, side: 'sell' as const, qty: '1', combo: 'c1', strategy: 'bull_call' as const },
			{ k: 'adv', n: 2 },
			{ k: 'order', side: 'buy' as const, qty: '2', combo: 'c2', strategy: 'covered_call' as const },
			{ k: 'opt', series: 9, side: 'sell' as const, qty: '2', combo: 'c2', strategy: 'covered_call' as const },
			{ k: 'cancel', combo: 'c2', strategy: 'covered_call' as const }
		];
		const out = combosFromLog(log, 60);
		expect(out.map((c) => [c.id, c.strategy, c.step, c.withdrawn, c.legs.map((l) => l.series)])).toEqual([
			['c1', 'bull_call', 63, false, [1, 3]],
			['c2', 'covered_call', 65, true, [0, 9]]
		]);
	});
});

const REC: ComboRecord = {
	id: 'c1',
	strategy: 'bull_call',
	step: 70,
	withdrawn: false,
	legs: [
		{ series: 1, side: 'buy', qty: '1' },
		{ series: 3, side: 'sell', qty: '1' }
	]
};

// The orders accepted at the combo's step (another order earlier in the step), then the open.
const placed = [
	ev({ seq: 1, step: 70, orderId: 4, side: 'buy', qty: '1.00000000' }),
	ev({ seq: 2, step: 70, orderId: 5, side: 'buy', qty: '1.00000000' }),
	ev({ seq: 3, step: 70, orderId: 6, side: 'sell', qty: '1.00000000' })
];

describe('combo status from the engine events', () => {
	it('finds the legs\' orders: the last run of accepted orders that matches the legs', () => {
		expect(comboOrderIds(placed, REC)).toEqual([5, 6]);
		expect(comboOrderIds([], REC)).toEqual([null, null]);
	});

	it('working until the open, then filled with the premium paid and received', () => {
		expect(comboStatus(placed, REC).state).toBe('working');
		const filled = [
			...placed,
			ev({ seq: 4, step: 71, type: 'fill', orderId: 5, series: 1, side: 'buy', price: '4.00000000', qty: '1' }),
			ev({ seq: 5, step: 71, type: 'fill', orderId: 6, series: 3, side: 'sell', price: '1.50000000', qty: '1' })
		];
		const s = comboStatus(filled, REC);
		expect(s.state).toBe('filled');
		expect(netPremium(s, 1)).toBeCloseTo(-2.5, 9);
		expect(openLegs(s)).toEqual([]);
	});

	it('a short leg cancelled at the open (no bid) after its long filled: incomplete, the long stays open', () => {
		const events = [
			...placed,
			ev({ seq: 4, step: 71, type: 'fill', orderId: 5, series: 1, side: 'buy', price: '4.00000000' }),
			ev({ seq: 5, step: 71, type: 'order', status: 'cancelled', reason: 'no_bid', orderId: 6, side: 'sell' })
		];
		const s = comboStatus(events, REC);
		expect(s.state).toBe('incomplete');
		expect(openLegs(s).map((l) => [l.leg.series, l.price, l.fillStep])).toEqual([[1, '4.00000000', 71]]);
		expect(s.legs[1]).toMatchObject({ state: 'cancelled', reason: 'no_bid' });
	});

	it('every leg cancelled at the open: not executed; withdrawn at placement wins', () => {
		const events = [
			...placed,
			ev({ seq: 4, step: 71, status: 'cancelled', reason: 'insufficient_margin', orderId: 5 }),
			ev({ seq: 5, step: 71, status: 'cancelled', reason: 'no_bid', orderId: 6, side: 'sell' })
		];
		expect(comboStatus(events, REC).state).toBe('not_executed');
		expect(comboStatus(events, { ...REC, withdrawn: true }).state).toBe('withdrawn');
	});
});

describe('debrief grouping', () => {
	const trade = (n: number, series: number, side: 'long' | 'short', entryStep: number, net: string) =>
		({ n, series, side, entryStep, net }) as EngineTrade;

	it('the trades a combo\'s legs opened go under it with their summed net result; the rest stay apart', () => {
		const events = [
			...placed,
			ev({ seq: 4, step: 71, type: 'fill', orderId: 5, series: 1, side: 'buy', price: '4' }),
			ev({ seq: 5, step: 71, type: 'fill', orderId: 6, series: 3, side: 'sell', price: '1.5' })
		];
		const trades = [trade(1, 0, 'long', 65, '10'), trade(2, 1, 'long', 71, '6'), trade(3, 3, 'short', 71, '-2.5'), trade(4, 1, 'long', 90, '1')];
		const g = groupTrades(trades, [comboStatus(events, REC)]);
		expect(g.combos).toHaveLength(1);
		expect(g.combos[0].trades.map((t) => t.n)).toEqual([2, 3]);
		expect(g.combos[0].net).toBeCloseTo(3.5, 9);
		expect(g.rest.map((t) => t.n)).toEqual([1, 4]);
	});
});
