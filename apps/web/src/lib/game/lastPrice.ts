import type { GameState } from '#lib/engine/types.ts';

/**
 * The last traded price: the close of the current bar. The engine reserves margin, checks brackets,
 * triggers stops and trailing stops and prices options against it, and fills at the next open from
 * it. `account.mark` is the same price unless the market has a mark-price series (pack schema 2,
 * policy v17), where it is the mark close and serves only margin, liquidation and funding; so order
 * estimates, side checks, the calibration question and the option underlying use this, never the mark.
 */
export function lastPrice(s: Pick<GameState, 'bars' | 'account'>): number {
	const bar = s.bars[s.bars.length - 1];
	return Number(bar ? bar[3] : s.account.mark);
}
