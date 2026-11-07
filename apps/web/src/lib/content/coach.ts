// The guided first trade (design D6, guided-first-trade spec): coach-mark steps as data. Each step
// names its target (a `data-coach` attribute on the real terminal or debrief) and what completes it:
// Next only, Next or a typed stop, Next or one advance. The texts describe controls and process only,
// never a market or a mission's outcome.

import type { Locale } from '#lib/game/i18n.svelte.ts';
import coachJson from './coach.json';

export type CoachTarget = 'chart' | 'ticket' | 'stop' | 'risk' | 'advance' | 'debrief';
export type CoachCompletes = 'next' | 'stop' | 'advance';

export interface CoachStep {
	id: string;
	target: CoachTarget;
	completes: CoachCompletes;
	text: Record<Locale, { title: string; body: string }>;
}

export interface CoachTour {
	id: string;
	steps: CoachStep[];
}

export const FIRST_TRADE: CoachTour = coachJson as CoachTour;

/** Steps shown in the terminal; the rest wait for that run's debrief. */
export const TERMINAL_STEPS = FIRST_TRADE.steps.filter((s) => s.target !== 'debrief').length;
