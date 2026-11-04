import { describe, expect, it } from 'vitest';
import { describeFinding } from './analysis.ts';
import type { EngineFinding } from '#lib/engine/types.ts';

const oversized: EngineFinding = { code: 'oversized', tone: 'warn', step: 61, count: 0, price: '0', requested: '0', amount: '444.00000000' };

describe('describeFinding', () => {
	it('regression: an oversized entry is not described as a plan (it may have had none)', () => {
		// Before: "Um plano arriscava ~444 cr" next to "entries without a recorded plan" when the plan was skipped.
		const pt = describeFinding(oversized, 'pt-BR');
		const en = describeFinding(oversized, 'en');
		expect(pt).toContain('~444 cr');
		expect(pt).not.toMatch(/plano/i);
		expect(en).toContain('~444 cr');
		expect(en).not.toMatch(/plan/i);
	});
});

describe('finding numbers follow the locale', () => {
	it('regression: a margin call reads 14,38 / 3.900,00 in PT-BR (was 14.38 / 3900.00)', () => {
		const mc: EngineFinding = { code: 'margin_called', tone: 'bad', step: 105, count: 0, price: '14.38000000', requested: '0', amount: '3900.00000000' };
		expect(describeFinding(mc, 'pt-BR')).toContain('a 14,38, perda de 3.900,00 cr');
		expect(describeFinding(mc, 'en')).toContain('at 14.38, a loss of 3,900.00 cr');
		const big: EngineFinding = { ...oversized, amount: '4574.25000000' };
		expect(describeFinding(big, 'pt-BR')).toContain('~4.574 cr');
	});
});
