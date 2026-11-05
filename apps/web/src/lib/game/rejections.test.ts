import { afterEach, describe, expect, it } from 'vitest';
import type { GameState } from '#lib/engine/types.ts';
import { game } from './fixtures.test-util.ts';
import { i18n } from './i18n.svelte.ts';
import { marginFacts, orderFacts, rejectionText, releverFacts } from './rejections.ts';

function withPosition(qty: string, entry: string, margin: string, leverage: number): GameState {
	return game([], {
		status: 'active',
		leverage,
		contract: { multiplier: '1', leverageCap: 10 } as GameState['contract'],
		account: { available: '1234.5' } as GameState['account'],
		position: { qty, entry, margin }
	});
}

afterEach(() => {
	i18n.locale = 'pt-BR';
});

describe('rejection messages with numbers', () => {
	it('insufficient margin says what was needed and what was available, in the locale', () => {
		const g = withPosition('0', '0', '0', 1);
		expect(rejectionText('insufficient_margin', orderFacts(g, 10_305.2, 1))).toBe(
			'margem insuficiente: precisava de 10.305,20 cr e havia 1.234,50 cr disponíveis'
		);
		i18n.locale = 'en';
		expect(rejectionText('insufficient_margin', orderFacts(g, 10_305.2, 1))).toBe(
			'insufficient margin: needed 10,305.20 cr, 1,234.50 cr available'
		);
	});

	it('a lower leverage on an open position needs the whole position re-margined', () => {
		// 50 long @ 100 at 5x holds 1,000; at 1x it needs 5,000: 4,000 more.
		const f = releverFacts(withPosition('50', '100', '1000', 5), 1);
		expect(f.need).toBeCloseTo(4_000, 6);
		expect(rejectionText('insufficient_margin', f)).toContain('precisava de 4.000,00 cr');
	});

	it('removing margin below the initial margin names both', () => {
		const g = withPosition('-50', '100', '1100', 5); // initial 1,000
		expect(rejectionText('margin_below_initial', marginFacts(g, -200))).toBe(
			'a margem ficaria em 900,00 cr, abaixo da margem inicial de 1.000,00 cr'
		);
		expect(marginFacts(g, 300)).toEqual({ need: 300, available: 1234.5 });
	});

	it('leverage above the cap names the cap; codes without numbers fall back to the plain reason', () => {
		expect(rejectionText('leverage_above_cap', { leverage: 25, cap: 10 })).toBe('alavancagem 25x acima do limite de 10x desta missão');
		expect(rejectionText('insufficient_margin')).toBe('margem insuficiente');
		expect(rejectionText('invalid_tick', { need: 1 })).toBe('preço fora do tick');
		expect(rejectionText('brand_new')).toBe('brand_new');
	});
});
