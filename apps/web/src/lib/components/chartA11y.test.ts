import { afterEach, describe, expect, it } from 'vitest';
import { i18n } from '#lib/game/i18n.svelte.ts';
import { barPage, candleText, chartSummary, stepFocus, type Ohlcv } from './chartA11y.ts';

afterEach(() => {
	i18n.locale = 'pt-BR';
});

describe('barPage', () => {
	it('the default page is the last one and ends with the current closed bar (UX01)', () => {
		expect(barPage(120, null, 50)).toEqual({ start: 70, end: 120, page: 2, pages: 3 });
		expect(barPage(121, null, 50)).toEqual({ start: 71, end: 121, page: 2, pages: 3 });
	});
	it('earlier pages step back by a page and the first is cut short', () => {
		expect(barPage(120, 1, 50)).toEqual({ start: 20, end: 70, page: 1, pages: 3 });
		expect(barPage(120, 0, 50)).toEqual({ start: 0, end: 20, page: 0, pages: 3 });
		expect(barPage(120, 9, 50).page).toBe(2); // clamped
	});
	it('short and empty charts have one page', () => {
		expect(barPage(7, null, 50)).toEqual({ start: 0, end: 7, page: 0, pages: 1 });
		expect(barPage(0, null, 50)).toEqual({ start: 0, end: 0, page: 0, pages: 1 });
	});
});

const bars: Ohlcv[] = [
	[100, 102, 99, 101, 1200],
	[101, 101.5, 98, 98.5, 900],
	[98.5, 104, 98.5, 103.53, 1500]
];

describe('chart text for screen readers', () => {
	it('announces a candle in the locale, with its move and patterns', () => {
		expect(candleText(41, bars[2], ['Engolfo de alta'], 'pt-BR')).toBe(
			'Barra #41: abertura 98,50, máxima 104,00, mínima 98,50, fechamento 103,53, volume 1.500, alta de 5,11%. Padrões: Engolfo de alta.'
		);
		i18n.locale = 'en';
		expect(candleText(1, bars[1], [], 'en')).toBe('Bar #1: open 101.00, high 101.50, low 98.00, close 98.50, volume 900, down 2.48%.');
	});

	it('summarises the latest bar, the move since the first and the recent range', () => {
		expect(chartSummary(bars, 'pt-BR')).toBe(
			'3 barras fechadas. Última, #2: fechamento 103,53, alta de 5,11% na barra; desde a primeira barra, alta de 2,50%. Últimas 3 barras: mínima 98,00, máxima 104,00.'
		);
	});

	it('says there is no data before any bar', () => {
		expect(chartSummary([], 'pt-BR')).toBe('Sem dados: nenhuma barra fechada ainda.');
	});
});

describe('stepFocus', () => {
	it('moves the focused candle with arrows, pages, Home and End, inside the closed bars', () => {
		expect(stepFocus('ArrowLeft', null, 30)).toBe(28);
		expect(stepFocus('ArrowRight', 29, 30)).toBe(29); // never past the current bar
		expect(stepFocus('PageUp', 5, 30)).toBe(0);
		expect(stepFocus('Home', 12, 30)).toBe(0);
		expect(stepFocus('End', 3, 30)).toBe(29);
		expect(stepFocus('Space', 3, 30)).toBeNull();
		expect(stepFocus('ArrowLeft', null, 0)).toBeNull();
	});
});
