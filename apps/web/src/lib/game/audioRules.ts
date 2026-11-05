// What the soundtrack should do (pure, unit-tested; spec terminal-ui "Music and sound effects").
//
// Music has a mood per screen: the menu theme outside a mission, the in-game theme while trading,
// and the tense theme while the account is in danger. A trade that closes plays a short win or
// loss sting; a liquidation always plays the loss sting.

import type { EngineEvent, GameState } from '#lib/engine/types.ts';

export type Mood = 'menu' | 'game' | 'tense';
export type Sting = 'win' | 'loss';
export type ScreenName = 'loading' | 'menu' | 'import' | 'tutorial' | 'briefing' | 'terminal' | 'debrief' | 'curriculum' | 'journal' | 'lesson' | 'catalog';

export const LIQ_DANGER = 0.05; // within 5% of the liquidation price
export const MARGIN_DANGER = 2; // option equity under 2x the maintenance margin
// Hysteresis: once tense, the danger must clear by a margin before the calm theme returns, so a
// price hovering at the threshold doesn't flip the music every bar.
export const RELIEF = 1.6;
export const ALERT_BARS = 3; // a margin call or liquidation keeps the tension for 3 bars

function nearLiquidation(g: GameState, k: number): boolean {
	const liq = g.account.liquidationPrice;
	const mark = Number(g.account.mark);
	if (liq == null || Number(g.position.qty) === 0 || mark <= 0) return false;
	return Math.abs(mark - Number(liq)) / mark < LIQ_DANGER * k;
}

function thinOptionMargin(g: GameState, k: number): boolean {
	const mm = Number(g.options?.maintenanceMargin ?? 0);
	return mm > 0 && Number(g.account.equity) < mm * MARGIN_DANGER * k;
}

function recentAlert(g: GameState): boolean {
	return g.events.some((e) => (e.type === 'margin_call' || e.type === 'liquidation') && g.step - e.step < ALERT_BARS);
}

export function inDanger(g: GameState, wasTense = false): boolean {
	const k = wasTense ? RELIEF : 1;
	return g.status === 'active' && (nearLiquidation(g, k) || thinOptionMargin(g, k) || recentAlert(g));
}

export function moodFor(screen: ScreenName, g: GameState | null, previous: Mood | null = null): Mood {
	if (screen !== 'terminal' || !g) return 'menu';
	return inDanger(g, previous === 'tense') ? 'tense' : 'game';
}

// A fill with realized P&L closed (part of) a position; settlements close option positions.
function realized(e: EngineEvent): number {
	if (e.type === 'fill' || e.type === 'option_settlement') return Number(e.pnl);
	return 0;
}

/** The sting for events newer than `afterSeq`, or null when nothing closed. */
export function stingFor(events: EngineEvent[], afterSeq: number): Sting | null {
	let pnl = 0;
	let closed = false;
	for (const e of events) {
		if (e.seq <= afterSeq) continue;
		if (e.type === 'liquidation') return 'loss';
		const r = realized(e);
		if (r !== 0) {
			pnl += r;
			closed = true;
		}
	}
	if (!closed) return null;
	return pnl > 0 ? 'win' : 'loss';
}

/**
 * Remembers which events were already heard per run. The first state seen for a run (a new
 * mission, or a resumed one replayed from storage) is a baseline: its history never plays.
 */
export class StingTracker {
	private run = '';
	private heard = 0;

	observe(runId: string, events: EngineEvent[]): Sting | null {
		const last = events.length ? events[events.length - 1].seq : 0;
		if (runId !== this.run) {
			this.run = runId;
			this.heard = last;
			return null;
		}
		const sting = stingFor(events, this.heard);
		this.heard = Math.max(this.heard, last);
		return sting;
	}
}
