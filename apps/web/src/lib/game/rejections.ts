// Rejection messages with the numbers behind them (spec terminal-ui "Distinct actions and
// acknowledgements": an insufficient_margin rejection explains the required and available margin).
// The engine answers with a reason code only; the client adds the figures it already shows on the
// ticket and the rail, so they are estimates for display, never state.

import type { GameState } from '#lib/engine/types.ts';
import { fmtMoney, num } from './format.ts';
import { i18n, reasonText, t } from './i18n.svelte.ts';

/** What the client knew when a command was refused. Every field is optional. */
export interface RejectionFacts {
	/** Margin (plus fee) the command needed. */
	need?: number;
	/** Available balance at the time. */
	available?: number;
	/** Isolated margin the position would have kept (removing margin). */
	margin?: number;
	/** Initial margin of the position at its leverage. */
	initial?: number;
	/** Leverage asked for and the mission cap. */
	leverage?: number;
	cap?: number;
}

const has = (...v: (number | undefined)[]): boolean => v.every((x) => x != null && Number.isFinite(x));

/** Plain-language text for a refused command, with its numbers in the player's locale when known. */
export function rejectionText(reason: string, f: RejectionFacts = {}): string {
	const L = i18n.locale;
	if (reason === 'insufficient_margin' && has(f.need, f.available)) {
		return t('reasonWhy.insufficient_margin', { need: fmtMoney(f.need!, L), avail: fmtMoney(f.available!, L) });
	}
	if (reason === 'margin_below_initial' && has(f.margin, f.initial)) {
		return t('reasonWhy.margin_below_initial', { margin: fmtMoney(f.margin!, L), initial: fmtMoney(f.initial!, L) });
	}
	if (reason === 'leverage_above_cap' && has(f.leverage, f.cap)) {
		return t('reasonWhy.leverage_above_cap', { lev: f.leverage!, cap: f.cap! });
	}
	return reasonText(reason);
}

const positionNotional = (g: GameState): number => Math.abs(num(g.position.qty)) * num(g.position.entry) * Number(g.contract.multiplier);

/** An order refused at placement: the margin the ticket estimated against the available balance. */
export function orderFacts(g: GameState, need: number, leverage: number): RejectionFacts {
	return { need, available: num(g.account.available), leverage, cap: g.contract.leverageCap };
}

/**
 * A new leverage refused for an open position: the engine re-margins the whole position at its entry,
 * so the extra margin is the new initial margin minus the margin it holds.
 */
export function releverFacts(g: GameState, leverage: number): RejectionFacts {
	const need = positionNotional(g) / Math.max(1, leverage) - num(g.position.margin);
	return { need, available: num(g.account.available), leverage, cap: g.contract.leverageCap };
}

/** Isolated margin added (delta > 0) or removed (delta < 0) and refused. */
export function marginFacts(g: GameState, delta: number): RejectionFacts {
	if (delta > 0) return { need: delta, available: num(g.account.available) };
	return { margin: num(g.position.margin) + delta, initial: positionNotional(g) / Math.max(1, g.leverage) };
}
