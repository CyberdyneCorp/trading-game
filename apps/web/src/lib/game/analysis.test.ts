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
