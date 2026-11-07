import { describe, expect, it } from 'vitest';
import { commandRequest, type CommandAction } from './session.svelte.ts';

// Combo tags (add-academy-and-strategies D3, task E6) are part of the logged action only: the request
// the engine worker receives is the same with or without them, so a tagged log replays to the same
// digest (the Python binding checks the digests of a tagged and an untagged log).
describe('combo tags never reach the engine', () => {
	const tag = { combo: 'c1', strategy: 'iron_condor' as const };
	const actions: CommandAction[] = [
		{ k: 'opt', series: 5, side: 'buy', type: 'market', qty: '1', premium: null },
		{ k: 'order', type: 'market', side: 'buy', qty: '2', price: null, ro: false, stop: null, tp: null, trail: null },
		{ k: 'cancel', id: 7 }
	];

	it.each(actions)('$k: the same engine request with and without the tag', (a) => {
		expect(commandRequest({ ...a, ...tag } as CommandAction, 3)).toEqual(commandRequest(a, 3));
	});
});
