// Debrief episodes panel (scoring v14): a result or an excursion in R, from the engine's basis points.

import type { PositionEpisode } from '#lib/engine/types.ts';
import { fmtNum } from './format.ts';
import type { Locale } from './i18n.svelte.ts';

/** A signed multiple of R, or a dash when the episode had no planned risk. */
export const rText = (bp: number | null, L: Locale) => (bp == null ? '—' : `${fmtNum(bp / 10_000, L, 2, true)}R`);

/** "MFE · MAE": the engine gives both excursions as sizes; the adverse one reads as a loss in R. */
export const excursionsText = (e: Pick<PositionEpisode, 'mfeRBp' | 'maeRBp'>, L: Locale) =>
	`${rText(e.mfeRBp, L)} · ${rText(e.maeRBp == null ? null : 0 - e.maeRBp, L)}`;
