import type { GameState, OptionQuoteView, OrderTypeName } from '#lib/engine/types.ts';
import { seriesLabel } from './optionsMath.ts';

export const ORDER_TYPE_LABEL: Record<OrderTypeName, string> = {
	market: 'MKT',
	limit: 'LMT',
	stop_market: 'STOP',
	take_profit: 'TP',
	trailing_stop: 'TRAIL'
};

/**
 * Names for ids that events carry: order types and option contracts. The state only lists live
 * orders and listed series, so names are remembered as they are seen and survive fills and expiry.
 */
export class EventNames {
	private orders = new Map<number, OrderTypeName>();
	private series = new Map<number, string>();

	reset(): void {
		this.orders.clear();
		this.series.clear();
	}

	observe(s: Pick<GameState, 'orders' | 'options'>): void {
		for (const o of s.orders) this.orders.set(o.id, o.type);
		for (const q of s.options.chain) this.rememberSeries(q);
	}

	rememberSeries(q: Pick<OptionQuoteView, 'id' | 'kind' | 'strike' | 'expiryStep'>): void {
		if (!this.series.has(q.id)) this.series.set(q.id, seriesLabel(q));
	}

	orderType(id: number): string | undefined {
		const type = this.orders.get(id);
		return type && ORDER_TYPE_LABEL[type];
	}

	seriesName(id: number): string {
		return this.series.get(id) ?? `OPT#${id}`;
	}
}
