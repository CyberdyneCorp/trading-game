import { describe, expect, it } from 'vitest';
import { EventNames } from './eventNames.ts';
import type { GameState } from '#lib/engine/types.ts';

type Seen = Pick<GameState, 'options'>;
const seen = (chain: Partial<GameState['options']['chain'][number]>[]): Seen => ({ options: { chain, positions: [] } }) as unknown as Seen;

describe('EventNames', () => {
	it('names an option series after it expires and falls back to its id', () => {
		const n = new EventNames();
		n.observe(seen([{ id: 5, kind: 'call', strike: '100.00000000', expiryStep: 96 }]));
		n.observe(seen([]));
		expect(n.seriesName(5)).toBe('C 100 · exp 96');
		expect(n.seriesName(7)).toBe('OPT#7');
	});

	it('restores names saved with a checkpoint, keeping those already seen', () => {
		const n = new EventNames();
		n.observe(seen([{ id: 5, kind: 'call', strike: '100.00000000', expiryStep: 96 }]));
		const saved = n.entries();
		expect(saved).toEqual([[5, 'C 100 · exp 96']]);

		const resumed = new EventNames();
		resumed.observe(seen([{ id: 9, kind: 'put', strike: '95.00000000', expiryStep: 136 }]));
		resumed.restore([...saved, [9, 'stale']]);
		expect(resumed.seriesName(5)).toBe('C 100 · exp 96');
		expect(resumed.seriesName(9)).toBe('P 95 · exp 136');
	});
});
