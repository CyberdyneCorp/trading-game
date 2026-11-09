// Portfolio view model (add-practice-and-advanced-strategies D9, W1-W8): the rows and totals the panel
// shows come from the engine's holdings what-if (tl_run_option_whatif with holdings: true) and its risk
// block; this module only chooses what to show and words the warnings. Nothing here is computed.

import type { GameState, RiskLimitCheck, RiskLimitKind, WhatIfLegView, WhatIfResult, WhatIfRiskView } from '#lib/engine/types.ts';
import { hedgeOrder, type OrderLeg } from './strategies.ts';

/** The request the panel and the rail refresh on every state change. */
export const PORTFOLIO_REQUEST = { holdings: true, legs: [] } as const;

export interface PortfolioRow {
	leg: WhatIfLegView;
	/** Signed contracts: > 0 long. */
	qty: number;
}

/** One row per held position (the perpetual and each option series), as the engine valued them. */
export function portfolioRows(r: Pick<WhatIfResult, 'legs'>): PortfolioRow[] {
	return r.legs.map((leg) => ({ leg, qty: Number(leg.qty) * (leg.side === 'buy' ? 1 : -1) }));
}

/** The limits a total is above (warnings, never blocks). */
export function exceededLimits(risk: Pick<WhatIfRiskView, 'limits'> | null | undefined): RiskLimitCheck[] {
	return (risk?.limits ?? []).filter((l) => l.exceeded);
}

/** The limits the mission declares, from the state (for the briefing and a panel without holdings). */
export function declaredLimits(game: Pick<GameState, 'missionRules'>): { kind: RiskLimitKind; bp: number }[] {
	const r = game.missionRules.riskLimits;
	if (!r) return [];
	const out: { kind: RiskLimitKind; bp: number }[] = [];
	if (r.deltaBp) out.push({ kind: 'delta', bp: r.deltaBp });
	if (r.gammaBp) out.push({ kind: 'gamma', bp: r.gammaBp });
	if (r.vegaBp) out.push({ kind: 'vega', bp: r.vegaBp });
	if (r.thetaBp) out.push({ kind: 'theta', bp: r.thetaBp });
	return out;
}

/** "Hedge delta" (design D9): the perpetual order to neutral, -deltaUnits rounded toward zero to the lot. */
export function hedgeTicket(game: GameState, risk: Pick<WhatIfRiskView, 'deltaUnits'> | null | undefined): OrderLeg | null {
	return risk ? hedgeOrder(game, Number(risk.deltaUnits)) : null;
}

/** Delta after the hedge fills (display): the units plus the hedge's units. */
export function deltaAfterHedge(game: GameState, risk: Pick<WhatIfRiskView, 'deltaUnits'>, hedge: OrderLeg): number {
	const units = Number(hedge.qty) * Number(game.contract.multiplier) * (hedge.side === 'buy' ? 1 : -1);
	return Math.round((Number(risk.deltaUnits) + units) * 1e8) / 1e8;
}
