import { describe, expect, it } from 'vitest';
import { tipPlacement } from './termTip.ts';

describe('tipPlacement', () => {
	it('opens below the term, at its left edge, when there is room', () => {
		expect(tipPlacement({ left: 100, top: 200, bottom: 216 }, 1440, 900)).toEqual({ left: 100, top: 220, maxWidth: 320 });
	});

	it('regression: a term near the right edge (the rail at 1440 px) keeps its box on screen', () => {
		// Before, the box started at the term and ended at x=1465 on a 1440 px screen.
		const p = tipPlacement({ left: 1145, top: 170, bottom: 186 }, 1440, 900);
		expect(p.left + p.maxWidth).toBeLessThanOrEqual(1440 - 8);
	});

	it('flips above the term near the bottom, and narrows on a phone', () => {
		expect(tipPlacement({ left: 20, top: 860, bottom: 876 }, 1440, 900)).toEqual({ left: 20, bottom: 44, maxWidth: 320 });
		expect(tipPlacement({ left: 300, top: 100, bottom: 116 }, 390, 844).maxWidth).toBe(312);
		expect(tipPlacement({ left: 300, top: 100, bottom: 116 }, 390, 844).left).toBe(390 - 312 - 8);
	});
});
