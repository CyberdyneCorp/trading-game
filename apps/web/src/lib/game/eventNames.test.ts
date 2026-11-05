import { afterEach, describe, expect, it } from 'vitest';
import { EventNames, orderPriceText } from './eventNames.ts';
import { i18n } from './i18n.svelte.ts';
import { seriesLabel } from './optionsMath.ts';
import type { GameState } from '#lib/engine/types.ts';

type Seen = Pick<GameState, 'options'>;
const seen = (chain: Partial<GameState['options']['chain'][number]>[]): Seen => ({ options: { chain, positions: [] } }) as unknown as Seen;

afterEach(() => {
	i18n.locale = 'pt-BR';
});

describe('EventNames', () => {
	it('names an option series after it expires and falls back to its id', () => {
		const n = new EventNames();
		n.observe(seen([{ id: 5, kind: 'call', strike: '100.00000000', expiryStep: 96 }]));
		n.observe(seen([]));
		expect(n.seriesName(5, 'pt-BR')).toBe('C 100 · exp 96');
		expect(n.seriesName(7, 'pt-BR')).toBe('OPT#7');
	});

	it('regression: a fractional strike is not rounded (the timeline said "C 98" for the 97,50 call)', () => {
		const n = new EventNames();
		n.observe(seen([{ id: 3, kind: 'call', strike: '97.50000000', expiryStep: 85 }]));
		expect(n.seriesName(3, 'pt-BR')).toBe('C 97,50 · exp 85');
		expect(n.seriesName(3, 'en')).toBe('C 97.50 · exp 85');
		expect(seriesLabel({ kind: 'put', strike: '102.50000000', expiryStep: 85 }, 'pt-BR')).toBe('P 102,50 · exp 85');
	});
	it('restores series saved with a checkpoint, keeping those already seen, named in the shown locale', () => {
		const n = new EventNames();
		n.observe(seen([{ id: 5, kind: 'call', strike: '97.50000000', expiryStep: 96 }]));
		const saved = n.entries();
		expect(saved).toEqual([[5, { kind: 'call', strike: '97.50000000', expiryStep: 96 }]]);

		const resumed = new EventNames();
		resumed.observe(seen([{ id: 9, kind: 'put', strike: '95.00000000', expiryStep: 136 }]));
		resumed.restore([...saved, [9, { kind: 'call', strike: '1.00000000', expiryStep: 1 }]]);
		expect(resumed.seriesName(5, 'pt-BR')).toBe('C 97,50 · exp 96');
		expect(resumed.seriesName(5, 'en')).toBe('C 97.50 · exp 96');
		expect(resumed.seriesName(9, 'pt-BR')).toBe('P 95 · exp 136');
	});
});

describe('orderPriceText', () => {
	it('regression: an option order shows its premium in the player language (EN showed "prêmio")', () => {
		const opt = { type: 'limit' as const, series: 4, price: '0.50000000' };
		expect(orderPriceText(opt, 'pt-BR')).toBe('prêmio 0,50');
		i18n.locale = 'en';
		expect(orderPriceText(opt, 'en')).toBe('premium 0.50');
		expect(orderPriceText({ ...opt, series: 0 }, 'en')).toBe('0.50');
		expect(orderPriceText({ ...opt, type: 'market' }, 'en')).toBe('@open');
	});

});
