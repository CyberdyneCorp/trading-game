// The builder's and the lab's requests checked against the engine kernel (the WASM build itself):
// what the client sends is what the engine values, and the numbers it shows are the engine's.

import { beforeAll, describe, expect, it } from 'vitest';
import type { ComboPreview, GameState, WhatIfResult } from '#lib/engine/types.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import { lastPrice } from './lastPrice.ts';
import { comboLegs, expiriesOf, orderLegs, strikesOf, templateLegs, TEMPLATES, templateShortable, whatifLegs } from './strategies.ts';
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
		for (const id of TEMPLATES) {
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
});
