import { render } from 'svelte/server';
import { describe, expect, it } from 'vitest';
import type { WhatIfResult } from '#lib/engine/types.ts';
import WhatIfTable from './WhatIfTable.svelte';

const result: WhatIfResult = {
	forward: '100',
	atExpiry: false,
	legs: [
		{
			kind: 'call', side: 'buy', qty: '1', series: 0, expiryStep: 0, strike: '100', entry: '6.85', value: '6.85', iv: '0.6',
			ivFloored: false, delta: '0.5343', gamma: '0.0231', theta: '-0.114', vega: '0.114', positionValue: '6.85', pnl: '0'
		}
	],
	net: { value: '6.85', pnl: '0', delta: '0.5343', gamma: '0.0231', theta: '-0.114', vega: '0.114' },
	expiry: null,
	today: []
} as unknown as WhatIfResult;

describe('WhatIfTable', () => {
	it('names the time unit of theta: a day in the Academy lab, a bar in a run (the engine step differs)', () => {
		const academy = render(WhatIfTable, { props: { result, step: 'day' } }).body;
		expect(academy).toContain('Θ/dia');
		expect(academy).toContain('a cada dia que passa');
		const run = render(WhatIfTable, { props: { result, step: 'bar' } }).body;
		expect(run).toContain('Θ/barra');
		expect(run).toContain('ν/ponto');
	});

	it('colours a P&L by what it shows: -0.004 reads 0,00 cr and is neither a gain nor a loss', () => {
		const leg = { ...result.legs[0], pnl: '-0.004' };
		const tiny = { ...result, legs: [leg], net: { ...result.net, pnl: '-0.004' } } as unknown as WhatIfResult;
		const body = render(WhatIfTable, { props: { result: tiny } }).body;
		expect(body).toContain('0,00 cr');
		expect(body).not.toMatch(/class="[^"]*\bdown\b/);
		const loss = { ...result, legs: [{ ...leg, pnl: '-0.01' }], net: { ...result.net, pnl: '-0.01' } } as unknown as WhatIfResult;
		expect(render(WhatIfTable, { props: { result: loss } }).body).toMatch(/class="[^"]*\bdown\b/);
	});
});
