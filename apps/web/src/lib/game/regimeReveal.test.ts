import { afterEach, describe, expect, it } from 'vitest';
import type { RegimeSignals } from '#lib/engine/types.ts';
import { fmtTally, regimeBand, regimeName, signalRows } from './debrief.ts';
import { i18n } from './i18n.svelte.ts';

const tally = (hits: number, samples: number) => ({ hits, samples });
const row = (start: number, bars: number, samples: number): RegimeSignals => ({
	start,
	bars,
	label: 'range',
	direction: 0,
	rsiFade: tally(0, samples),
	bollingerFade: tally(0, 0),
	macdFollow: tally(0, 0),
	smaFollow: tally(0, 0)
});

afterEach(() => {
	i18n.locale = 'pt-BR';
});

describe('regime reveal in the debrief (G4, scoring v9)', () => {
	it('clips the spans to the decision window and lays them out as shares of it', () => {
		const band = regimeBand(
			[
				{ start: 0, bars: 70, label: 'range', direction: 0 },
				{ start: 70, bars: 40, label: 'trend', direction: -1 },
				{ start: 110, bars: 70, label: 'high_vol', direction: 0 }
			],
			60,
			139
		);
		expect(band.map((p) => [p.start, p.bars, p.label])).toEqual([
			[60, 10, 'range'],
			[70, 40, 'trend'],
			[110, 30, 'high_vol']
		]);
		expect(band.map((p) => p.left)).toEqual([0, 10 / 80, 50 / 80]);
		expect(band.reduce((s, p) => s + p.width, 0)).toBeCloseTo(1);
		expect(regimeBand([{ start: 0, bars: 50, label: 'range', direction: 0 }], 60, 70)).toEqual([]); // before the window
	});

	it('names a regime with its drawn direction, in both languages', () => {
		expect(regimeName('trend', 1)).toBe('Tendência de alta');
		expect(regimeName('low_liquidity')).toBe('Livro fino');
		i18n.locale = 'en';
		expect(regimeName('trend', -1)).toBe('Trend down');
		expect(regimeName('high_vol')).toBe('High volatility');
	});

	it('formats hits with a PT-BR percentage, and no samples as a dash', () => {
		expect(fmtTally(tally(3, 7), 'pt-BR')).toBe('3 de 7 (43%)');
		expect(fmtTally(tally(0, 0), 'pt-BR')).toBe('—');
		i18n.locale = 'en';
		expect(fmtTally(tally(3, 7), 'en')).toBe('3 of 7 (43%)');
	});

	it('lists only the spans where some signal fired', () => {
		expect(signalRows([row(60, 10, 0), row(70, 20, 4)]).map((r) => r.start)).toEqual([70]);
		expect(signalRows(undefined)).toEqual([]);
	});
});
