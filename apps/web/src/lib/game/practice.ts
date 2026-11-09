// Practice formats shared helpers (add-practice-and-advanced-strategies D16, D18, D19): the mission list
// without the round markets (menus, the campaign, the curriculum and the daily pool never show them),
// the round markets themselves, and the mistake retry's report.

import type { MissionInfo } from '#lib/engine/types.ts';

/** The built-in missions (tl_missions without the practice markets). */
export const missionsOnly = (list: readonly MissionInfo[]): MissionInfo[] => list.filter((m) => !m.practice);

/** The guess rounds' markets (practice "round"). */
export const roundMarkets = (list: readonly MissionInfo[]): MissionInfo[] => list.filter((m) => m.practice === 'round');

/** A mistake retry's comparison with its original attempt (design D18), shown at the retry's debrief. */
export interface MistakeReport {
	code: string;
	step: number;
	outcome: 'repeated' | 'avoided';
	/** The original attempt's result (null when its summary is gone). */
	original: { score: number | null; pnl: string } | null;
	retry: { score: number | null; pnl: string };
}
