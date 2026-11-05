import type { GameState, OptionQuoteView, OrderTypeName, OrderView } from '#lib/engine/types.ts';
import { fmtPrice } from './format.ts';
import { t, type Locale } from './i18n.svelte.ts';
import { seriesLabel } from './optionsMath.ts';

export const ORDER_TYPE_LABEL: Record<OrderTypeName, string> = {
	market: 'MKT',
	limit: 'LMT',
	stop_market: 'STOP',
	take_profit: 'TP',
	trailing_stop: 'TRAIL'
};

/** Price column of an open order: "@open" for a market order, the premium of an option order. */
export function orderPriceText(o: Pick<OrderView, 'type' | 'series' | 'price'>, locale: Locale): string {
	if (o.type === 'market') return '@open';
	const price = fmtPrice(o.price, locale);
	return o.series ? t('orders.premium', { price }) : price;
}

/**
 * Names for option contracts that events carry. The state only lists listed series, so names are
 * remembered as they are seen and survive expiry. (Order types come with each event: `orderType`.)
 */
/** What names an option series: its kind, strike and expiry. */
export type SeriesKey = Pick<OptionQuoteView, 'kind' | 'strike' | 'expiryStep'>;

export class EventNames {
	private series = new Map<number, SeriesKey>();

	reset(): void {
		this.series.clear();
	}

	observe(s: Pick<GameState, 'options'>): void {
		for (const q of s.options.chain) this.rememberSeries(q);
	}

	rememberSeries(q: Pick<OptionQuoteView, 'id' | 'kind' | 'strike' | 'expiryStep'>): void {
		if (!this.series.has(q.id)) this.series.set(q.id, { kind: q.kind, strike: q.strike, expiryStep: q.expiryStep });
	}

	/** The series seen so far, as [series id, series] pairs (saved with a checkpoint). */
	entries(): [number, SeriesKey][] {
		return [...this.series];
	}

	/**
	 * Adds series saved earlier (a checkpoint restore skips the states that listed series which have
	 * expired since, so their names come from the checkpoint instead).
	 */
	restore(entries: readonly [number, SeriesKey][]): void {
		for (const [id, q] of entries) if (!this.series.has(id)) this.series.set(id, q);
	}

	/** Named in the locale it is shown in, so a language switch renames remembered series too. */
	seriesName(id: number, locale: Locale): string {
		const q = this.series.get(id);
		return q ? seriesLabel(q, locale) : `OPT#${id}`;
	}
}
