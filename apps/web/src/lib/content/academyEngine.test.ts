// Engine-checked lesson numbers (design D2 "Correct numbers by construction"): every option value or
// Greek a lesson states is a `whatif` check on one of its steps' what-if widgets, valued here by the
// WASM build exactly as the widget asks for it; the expiry arithmetic (payoff, breakevens, maximum
// profit and loss) agrees with the engine's own expiry analytics. bindings/python/tests/
// test_academy_numbers.py checks the same through the Python binding.

import { beforeAll, describe, expect, it } from 'vitest';
import type { WhatIfResult } from '#lib/engine/types.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import { academyRequest, ZERO_SHIFT, type AcademyLeg } from '#lib/game/whatifLab.ts';
import { loadAllLessons, type Lesson, type PayoffLeg, type WhatIfWidget } from './academy.ts';
import { fieldOf, isEngineCheck, whatifCheckRequest, type Check } from './checks.ts';
import { GLOSSARY } from './glossary.ts';

const lessons = await loadAllLessons();
let engine: NodeEngine;

function widgetOf(l: Lesson, step: string): WhatIfWidget {
	const w = l.steps.find((s) => s.id === step)?.widget;
	if (w?.kind !== 'whatif') throw new Error(`${l.id}: step ${step} has no what-if widget`);
	return w;
}

/** A payoff check's legs as Academy legs (one expiry; the IV and tenor do not change expiry analytics). */
function asAcademy(legs: PayoffLeg[]): AcademyLeg[] {
	return legs.map((g) => ({ kind: g.kind, side: g.side, qty: g.qty, strike: g.strike ?? 0, days: 30, ivPct: 50, entry: g.price }));
}

const within = (got: number | null, expect: number, digits: number) => got != null && Math.abs(got - expect) <= 0.5 * 10 ** -digits + 1e-9;

function engineExpiry(legs: PayoffLeg[]): NonNullable<WhatIfResult['expiry']> {
	const strikes = legs.flatMap((g) => (g.strike ? [g.strike] : [g.price]));
	const forward = strikes.reduce((a, b) => a + b, 0) / strikes.length;
	const r = engine.whatif(academyRequest(asAcademy(legs), ZERO_SHIFT, forward)) as WhatIfResult;
	return r.expiry!;
}

function checkExpiry(c: Check, where: string) {
	if (c.kind === 'breakeven') expect(engineExpiry(c.legs).breakevens.map(Number), where).toEqual(c.expect.map(Number));
	if (c.kind === 'maxProfit') expect(engineExpiry(c.legs).maxProfit == null ? null : Number(engineExpiry(c.legs).maxProfit), where).toBe(c.expect);
	if (c.kind === 'maxLoss') expect(engineExpiry(c.legs).maxLoss == null ? null : Number(engineExpiry(c.legs).maxLoss), where).toBe(c.expect);
}

describe.skipIf(!wasmBuilt)('lesson numbers against the engine kernel (WASM)', () => {
	beforeAll(async () => {
		engine = await loadEngine();
	});

	it('every whatif check states what the engine gives for that widget and slider position', () => {
		let n = 0;
		for (const l of lessons) {
			for (const c of l.checks ?? []) {
				if (!isEngineCheck(c)) continue;
				const r = engine.whatif(whatifCheckRequest(c, widgetOf(l, c.step)));
				expect(within(fieldOf(r, c.field), c.expect, c.digits ?? 8), `${l.id} ${c.step} ${JSON.stringify(c.shift ?? {})} ${c.field}: ${fieldOf(r, c.field)}`).toBe(true);
				n += 1;
			}
		}
		expect(n).toBeGreaterThanOrEqual(15);
	});

	it('a what-if widget prices its legs at the engine value it states (the entry is the model price to the cent)', () => {
		for (const l of lessons)
			for (const s of l.steps) {
				if (s.widget?.kind !== 'whatif') continue;
				const r = engine.whatif(academyRequest(s.widget.legs, ZERO_SHIFT, s.widget.forward)) as WhatIfResult;
				s.widget.legs.forEach((g, i) => g.kind !== 'perp' && expect(Number(r.legs[i].value).toFixed(2), `${l.id} ${s.id}`).toBe(g.entry.toFixed(2)));
			}
	});

	it('the expiry arithmetic of lessons and glossary examples agrees with the engine analytics', () => {
		for (const l of lessons) for (const c of l.checks ?? []) checkExpiry(c, l.id);
		for (const term of GLOSSARY) for (const c of term.checks ?? []) checkExpiry(c, term.id);
	});
});
