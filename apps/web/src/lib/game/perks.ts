// Intel perks (spec progression-rewards "Intel perks unlocked by completed missions", design D4).
//
// Perks are modeled on legal products: an economic calendar and a low-latency news feed. They
// unlock by the number of distinct built-in missions finished on this device and are start options
// of a run (the engine shows the calendar and early headlines; bars never change). They apply to
// free play only: campaign missions and practice on imported markets start without them, so
// campaign runs stay comparable. A run played with a perk is labeled "with intel".

import type { RunOptionsView } from '#lib/engine/types.ts';

export type PerkId = 'calendar' | 'wire1' | 'wire2';

export interface Perk {
	id: PerkId;
	/** Distinct finished missions needed. */
	missions: number;
	/** Fast wire: headlines this many bars before their release (0 = not a wire). */
	lead: number;
}

export const PERKS: readonly Perk[] = [
	{ id: 'calendar', missions: 1, lead: 0 },
	{ id: 'wire1', missions: 2, lead: 1 },
	{ id: 'wire2', missions: 4, lead: 2 }
];

/** Distinct built-in missions among the finished ones (imported packs do not count). */
export function distinctFinished(finished: readonly string[], builtIn: readonly string[]): number {
	return new Set(finished.filter((m) => builtIn.includes(m))).size;
}

export function unlockedPerks(finishedCount: number): PerkId[] {
	return PERKS.filter((p) => finishedCount >= p.missions).map((p) => p.id);
}

/** Engine start options for the enabled perks (the longest enabled wire wins). */
export function perkOptions(enabled: readonly PerkId[]): Pick<RunOptionsView, 'newsLead' | 'calendar'> {
	const on = PERKS.filter((p) => enabled.includes(p.id));
	return { calendar: enabled.includes('calendar'), newsLead: Math.max(0, ...on.map((p) => p.lead)) };
}

/** Perks a run's start options used, for the "with intel" label (debrief, run record). */
export function perksUsed(o: Partial<Pick<RunOptionsView, 'newsLead' | 'calendar'>> | undefined): PerkId[] {
	const used: PerkId[] = o?.calendar ? ['calendar'] : [];
	const wire = PERKS.find((p) => p.lead > 0 && p.lead === o?.newsLead);
	return wire ? [...used, wire.id] : used;
}

export const withIntel = (o: Partial<Pick<RunOptionsView, 'newsLead' | 'calendar'>> | undefined): boolean =>
	!!o?.calendar || (o?.newsLead ?? 0) > 0;

/** Unlocked perks the player has not been told about yet (the next free-play briefing explains them). */
export function newPerks(unlocked: readonly PerkId[], introduced: readonly string[]): PerkId[] {
	return unlocked.filter((p) => !introduced.includes(p));
}
