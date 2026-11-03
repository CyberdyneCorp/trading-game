// Per-trade breakdown for the debrief: a trade runs from flat to flat (a flip closes one and opens
// the next). R multiple = net P&L / planned risk from the plan recorded before the entry.

import type { EngineEvent, GameState } from '#lib/engine/types.ts';
import type { PlanEntry } from './analysis.ts';

export type ExitReason = 'stop' | 'target' | 'manual' | 'liquidation' | 'open';

export interface TradeRow {
	n: number;
	side: 'long' | 'short';
	entryStep: number;
	exitStep: number | null;
	qty: number;
	entryAvg: number;
	exitAvg: number | null;
	pnl: number; // realized, before fees
	fees: number;
	net: number;
	bars: number;
	exitReason: ExitReason;
	plannedRisk: number | null;
	r: number | null;
	planRespected: boolean | null;
}

interface Open {
	row: TradeRow;
	entryNotional: number;
	exitQty: number;
	exitNotional: number;
}

const n = (d: string) => Number(d);

function exitReasonOf(trade: TradeRow, fill: EngineEvent): ExitReason {
	if (n(fill.requested) === 0) return 'manual';
	const favorable = trade.side === 'long' ? n(fill.requested) > trade.entryAvg : n(fill.requested) < trade.entryAvg;
	return favorable ? 'target' : 'stop';
}

function planFor(journal: PlanEntry[], entryStep: number, after: number): PlanEntry | null {
	const candidates = journal.filter((p) => p.step <= entryStep && p.step >= after);
	return candidates.length ? candidates[candidates.length - 1] : null;
}

function finalize(o: Open, journal: PlanEntry[], after: number, lastStep: number): TradeRow {
	const t = o.row;
	t.exitAvg = o.exitQty > 0 ? o.exitNotional / o.exitQty : null;
	t.net = t.pnl - t.fees;
	t.bars = (t.exitStep ?? lastStep) - t.entryStep;
	const plan = planFor(journal, t.entryStep, after);
	t.plannedRisk = plan?.plannedRisk ?? null;
	t.r = t.plannedRisk && t.plannedRisk > 0 && t.exitStep != null ? t.net / t.plannedRisk : null;
	if (plan?.invalidation != null && t.exitAvg != null && t.exitReason !== 'open') {
		const tolerance = 0.01 * plan.invalidation;
		t.planRespected =
			t.side === 'long' ? t.exitAvg >= plan.invalidation - tolerance : t.exitAvg <= plan.invalidation + tolerance;
	}
	return t;
}

function openTrade(count: number, e: EngineEvent, qty: number): Open {
	return {
		row: {
			n: count,
			side: e.side === 'buy' ? 'long' : 'short',
			entryStep: e.step,
			exitStep: null,
			qty: Math.abs(qty),
			entryAvg: n(e.price),
			exitAvg: null,
			pnl: 0,
			fees: 0,
			net: 0,
			bars: 0,
			exitReason: 'open',
			plannedRisk: null,
			r: null,
			planRespected: null
		},
		entryNotional: Math.abs(qty) * n(e.price),
		exitQty: 0,
		exitNotional: 0
	};
}

export function buildTrades(state: GameState, journal: PlanEntry[]): TradeRow[] {
	const out: TradeRow[] = [];
	let pos = 0;
	let cur: Open | null = null;
	let lastExit = -1;
	const close = (step: number, reason: ExitReason) => {
		if (!cur) return;
		cur.row.exitStep = step;
		cur.row.exitReason = reason;
		out.push(finalize(cur, journal, lastExit, state.step));
		lastExit = step;
		cur = null;
	};
	for (const e of state.events) {
		if (e.type === 'liquidation' && cur) {
			const c: Open = cur;
			c.row.pnl += n(e.pnl);
			c.exitQty += n(e.qty);
			c.exitNotional += n(e.qty) * n(e.price);
			pos = 0;
			close(e.step, 'liquidation');
			continue;
		}
		if (e.type !== 'fill') continue;
		const q = (e.side === 'buy' ? 1 : -1) * n(e.qty);
		const next = pos + q;
		if (cur === null) cur = openTrade(out.length + 1, e, q);
		else if (Math.sign(q) === Math.sign(pos)) {
			const c: Open = cur;
			c.entryNotional += Math.abs(q) * n(e.price);
			c.row.qty += Math.abs(q);
			c.row.entryAvg = c.entryNotional / c.row.qty;
		} else {
			const c: Open = cur;
			const closing = Math.min(Math.abs(q), Math.abs(pos));
			c.exitQty += closing;
			c.exitNotional += closing * n(e.price);
			c.row.pnl += n(e.pnl);
		}
		(cur as Open).row.fees += n(e.amount);
		if (pos !== 0 && (next === 0 || Math.sign(next) !== Math.sign(pos))) {
			const reason = exitReasonOf((cur as Open).row, e);
			close(e.step, reason);
			if (next !== 0) cur = openTrade(out.length + 1, e, next);
		}
		pos = next;
	}
	if (cur) out.push(finalize(cur, journal, lastExit, state.step));
	return out;
}
