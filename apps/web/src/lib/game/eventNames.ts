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
 * Names for option contracts that events carry. The state only lists listed series, so names are
 * remembered as they are seen and survive expiry. (Order types come with each event: `orderType`.)
 */
export class EventNames {
	private series = new Map<number, string>();

	reset(): void {
		this.series.clear();
	}

	observe(s: Pick<GameState, 'options'>): void {
		for (const q of s.options.chain) this.rememberSeries(q);
	}

	rememberSeries(q: Pick<OptionQuoteView, 'id' | 'kind' | 'strike' | 'expiryStep'>): void {
		if (!this.series.has(q.id)) this.series.set(q.id, seriesLabel(q));
	}

	/** The names seen so far, as [series id, name] pairs (saved with a checkpoint). */
	entries(): [number, string][] {
		return [...this.series];
	}

	/**
	 * Adds names saved earlier (a checkpoint restore skips the states that listed series which have
	 * expired since, so their names come from the checkpoint instead).
	 */
	restore(entries: readonly [number, string][]): void {
		for (const [id, name] of entries) if (!this.series.has(id)) this.series.set(id, name);
	}

	seriesName(id: number): string {
		return this.series.get(id) ?? `OPT#${id}`;
	}
}
