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
export class EventNames {
	private series = new Map<number, Pick<OptionQuoteView, 'kind' | 'strike' | 'expiryStep'>>();

	reset(): void {
		this.series.clear();
	}

	observe(s: Pick<GameState, 'options'>): void {
		for (const q of s.options.chain) this.rememberSeries(q);
	}

	rememberSeries(q: Pick<OptionQuoteView, 'id' | 'kind' | 'strike' | 'expiryStep'>): void {
		if (!this.series.has(q.id)) this.series.set(q.id, { kind: q.kind, strike: q.strike, expiryStep: q.expiryStep });
	}

	/** Named in the locale it is shown in, so a language switch renames remembered series too. */
	seriesName(id: number, locale: Locale): string {
		const q = this.series.get(id);
		return q ? seriesLabel(q, locale) : `OPT#${id}`;
	}
}
