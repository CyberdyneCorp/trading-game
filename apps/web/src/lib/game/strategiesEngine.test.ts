// The builder's and the lab's requests checked against the engine kernel (the WASM build itself):
// what the client sends is what the engine values, and the numbers it shows are the engine's.

import { beforeAll, describe, expect, it } from 'vitest';
import type { ComboPreview, GameState, WhatIfResult } from '#lib/engine/types.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import { lastPrice } from './lastPrice.ts';
import {
	comboLegs,
	defaultLimit,
	expiriesOf,
	orderLegs,
	rollOrders,
	rollTargets,
	strikesOf,
	templateLegs,
	TEMPLATES,
	templateShortable,
	TWO_EXPIRY,
	twoExpiryTemplateLegs,
	whatifLegs
} from './strategies.ts';
import { analyticsOf } from './whatifLab.ts';
import { academyRequest, runRequest, ZERO_SHIFT, type AcademyLeg } from './whatifLab.ts';

let engine: NodeEngine;

describe.skipIf(!wasmBuilt)('builder and lab against the engine kernel (WASM)', () => {
	beforeAll(async () => {
		engine = await loadEngine();
	});

	function m05(): { run: number; state: GameState } {
		const run = engine.createRun('m05-options-writer', 3);
		return { run, state: engine.state(run) as GameState };
	}

	it('the lab at zero shift agrees with the chain: each leg rounds to the mid, Greeks equal the quote', () => {
		const { run, state } = m05();
		const listed = state.options.chain.filter((q) => q.listed);
		expect(listed.length).toBeGreaterThan(10);
		for (const q of listed) {
			const r = engine.runWhatif(run, runRequest([{ series: q.id, side: 'buy', qty: '1' }], ZERO_SHIFT, lastPrice(state))) as WhatIfResult;
			const leg = r.legs[0];
			expect(Math.round(Number(leg.value) * 100)).toBe(Math.round(Number(q.mid) * 100));
			expect([leg.delta, leg.gamma, leg.theta, leg.vega, leg.iv]).toEqual([q.delta, q.gamma, q.theta, q.vega, q.iv]);
		}
	});

	it('the price slider changes engine numbers; the time slider to expiry leaves no time value', () => {
		const legs: AcademyLeg[] = [{ kind: 'call', side: 'buy', strike: 100, qty: 1, days: 30, ivPct: 60, entry: 6.85 }];
		const at = (price: number, time = 0) => engine.whatif(academyRequest(legs, { price, time, iv: 0 })) as WhatIfResult;
		expect(Number(at(10).legs[0].value)).toBeGreaterThan(Number(at(0).legs[0].value));
		const end = at(5, 30);
		expect(end.atExpiry).toBe(true);
		expect(end.legs[0].value).toBe('5.00000000');
		expect([end.legs[0].gamma, end.legs[0].theta, end.legs[0].vega].map(Number)).toEqual([0, 0, 0]);
		expect(end.today).toEqual(end.expiry!.curve);
	});

	it('every template on the m05 chain: the dry run accepts it and leaves the state alone; the analysis is bounded or names the side', () => {
		const { run, state } = m05();
		const expiry = expiriesOf(state.options.chain)[0];
		const strikes = strikesOf(state.options.chain, expiry);
		for (const id of TEMPLATES.filter((x) => !TWO_EXPIRY.has(x))) {
			for (const short of [false, true]) {
				const legs = templateLegs(id, strikes, lastPrice(state), 1, short)!;
				const orders = orderLegs(state, expiry, legs)!;
				const preview = engine.comboPreview(run, { legs: comboLegs(orders) }) as ComboPreview;
				expect(preview.accepted, `${id} short=${short}`).toBe(true);
				const w = engine.runWhatif(run, { legs: whatifLegs(orders, null) }) as WhatIfResult;
				const unbounded = w.expiry!.maxLoss == null;
				// Only uncovered short calls can lose without bound (the perpetual covers the covered call).
				expect(unbounded, `${id} short=${short}`).toBe(short && templateShortable(id));
			}
		}
		expect((engine.state(run) as GameState).digest).toBe(state.digest);
	});

	it('Regression: a covered call on a held long with a stop plans the loss to that stop, not the whole long (engine)', () => {
		// Playtest: "one entry risked ~4,650 cr (> 2%)" for a call written on a 48-unit long that had its own stop.
		const { run, state } = m05();
		const expiry = expiriesOf(state.options.chain)[0];
		const strikes = strikesOf(state.options.chain, expiry);
		const f = lastPrice(state);
		const call = templateLegs('covered_call', strikes, f, 48)!.filter((l) => l.kind !== 'perp');
		const orders = orderLegs(state, expiry, call)!;
		const entry = f.toFixed(2);
		const stop = (f * 0.97).toFixed(2);
		const naked = engine.runWhatif(run, { legs: whatifLegs(orders, { qty: '48', entry }) }) as WhatIfResult;
		const covered = engine.runWhatif(run, { legs: whatifLegs(orders, { qty: '48', entry, stop }) }) as WhatIfResult;
		const premium = Number(covered.legs[0].entry);
		expect(Number(naked.expiry!.maxLoss)).toBeGreaterThan(40 * f);
		expect(Number(covered.expiry!.maxLoss)).toBeCloseTo((Number(entry) - Number(stop) - premium) * 48, 6);
		expect(Number(covered.expiry!.maxLoss)).toBeLessThan(0.02 * Number(state.account.equity));
	});

	it('the strategy comparison crosses only in the debrief, which the engine refuses while the run is active', () => {
		const run = engine.createRun('m16-range-condor', 3);
		expect(engine.debrief(run)).toEqual({ error: 'run_active' });
		expect(Object.keys(engine.state(run) as object)).not.toContain('strategyComparison');
		engine.advance(run, 100000);
		const d = engine.debrief(run) as { strategyComparison: { basis: string; rows: { name: string; netPnl: string }[] } };
		expect(d.strategyComparison.basis).toBe('reference');
		expect(d.strategyComparison.rows.map((r) => r.name)).toEqual(['short_strangle', 'iron_condor']);
		const other = engine.createRun('m05-options-writer', 3);
		engine.advance(other, 100000);
		expect((engine.debrief(other) as { strategyComparison: unknown }).strategyComparison).toBeNull();
	});
});


// add-practice-and-advanced-strategies wave 1: calendars and diagonals on two expiries, limit legs and a
// roll, sent exactly as the builder sends them and valued by the engine.
describe.skipIf(!wasmBuilt)('two-expiry structures, limit legs and rolls against the engine (WASM)', () => {
	beforeAll(async () => {
		engine ??= await loadEngine();
	});

	function m17(): { run: number; state: GameState; near: number; far: number } {
		const run = engine.createRun('m17-before-expiry', 3);
		const state = engine.state(run) as GameState;
		const [near, far] = expiriesOf(state.options.chain);
		return { run, state, near, far };
	}

	it('every calendar and diagonal: the dry run accepts it; the horizon at the first expiry bounds the long ones by their debit', () => {
		const { run, state, near, far } = m17();
		expect(far).toBeGreaterThan(near);
		const f = lastPrice(state);
		for (const id of [...TWO_EXPIRY]) {
			const legs = twoExpiryTemplateLegs(id, { step: near, strikes: strikesOf(state.options.chain, near) }, { step: far, strikes: strikesOf(state.options.chain, far) }, f, 1)!;
			const orders = orderLegs(state, near, legs)!;
			const preview = engine.comboPreview(run, { legs: comboLegs(orders) }) as ComboPreview;
			expect(preview.accepted, id).toBe(true);
			const w = engine.runWhatif(run, { legs: whatifLegs(orders, null) }) as WhatIfResult;
			expect(w.expiry, id).toBeNull();
			const a = analyticsOf(w)!;
			expect(a.horizonStep, id).toBe(near - state.step);
			expect(a.maxLoss, id).not.toBeNull();
			const debit = -Number(preview.netPremium);
			expect(Number(a.maxLoss), id).toBeLessThanOrEqual(debit + 1e-6); // the long versions: at most the debit
		}
		expect((engine.state(run) as GameState).digest).toBe(state.digest);
	});

	it('limit legs at the passive mid with IOC: the dry run accepts them and values them at the limit', () => {
		const { run, state, near } = m17();
		const legs = templateLegs('bull_call', strikesOf(state.options.chain, near), lastPrice(state), 1)!.map((l) => {
			const q = state.options.chain.find((x) => x.expiryStep === near && x.kind === l.kind && Number(x.strike) === l.strike)!;
			return { ...l, limit: defaultLimit(q, l.side, Number(state.options.premiumTick)) };
		});
		const orders = orderLegs(state, near, legs)!;
		const preview = engine.comboPreview(run, { legs: comboLegs(orders, { tif: 'ioc' }) }) as ComboPreview;
		expect(preview.accepted).toBe(true);
		const premium = (o: (typeof orders)[number]) => Number(o.premium) * Number(o.qty) * Number(state.options.multiplier);
		expect(Number(preview.legs[0].premium)).toBeCloseTo(premium(orders[0]), 6);
		const w = engine.runWhatif(run, { legs: whatifLegs(orders, null) }) as WhatIfResult;
		expect(w.legs[0].entry).toBe(`${Number(orders[0].premium).toFixed(8)}`);
	});

	it('a roll of a held calendar: closing the near short first is accepted by the dry run', () => {
		const { run, state, near, far } = m17();
		const last = expiriesOf(state.options.chain).at(-1)!;
		const f = lastPrice(state);
		const legs = twoExpiryTemplateLegs('calendar_call', { step: near, strikes: strikesOf(state.options.chain, near) }, { step: last, strikes: strikesOf(state.options.chain, last) }, f, 1)!;
		const series = orderLegs(state, near, legs)!;
		// Holding the calendar: short near, long on the last expiry (as the combo's filled legs would be).
		const held = series.map((o) => ({ series: o.series, qty: o.side === 'buy' ? 1 : -1 }));
		const plan = rollOrders(state, held, far, false, f)!;
		expect(plan.from).toBe(near);
		expect(plan.orders[0].side).toBe('buy'); // the near short is bought back before anything is written
		expect(plan.orders.at(-1)!.side).toBe('sell');
		const preview = engine.comboPreview(run, { legs: comboLegs(plan.orders) }) as ComboPreview;
		expect(preview.accepted).toBe(true);
	});

	it("a calendar's short is never rolled onto its long's expiry (playtest: the roll closed the calendar)", () => {
		const { state, near, far } = m17();
		const f = lastPrice(state);
		const legs = twoExpiryTemplateLegs('calendar_call', { step: near, strikes: strikesOf(state.options.chain, near) }, { step: far, strikes: strikesOf(state.options.chain, far) }, f, 1)!;
		const held = orderLegs(state, near, legs)!.map((o) => ({ series: o.series, qty: o.side === 'buy' ? 1 : -1 }));
		const later = expiriesOf(state.options.chain).filter((e) => e > far);
		expect(later.length).toBeGreaterThan(0);
		expect(rollTargets(state.options.chain, near, [near, far])).toEqual(later);
		expect(rollOrders(state, held, far, false, f)).toBeNull();
		expect(rollOrders(state, held, later[0], false, f)!.orders).toHaveLength(2);
	});
});
