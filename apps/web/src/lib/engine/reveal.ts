// Leak prevention for the seeded reveal (design D6, D8.1 G4). The reveal names the regimes and event
// outcomes of the whole run, so the worker hands it out only once the run can no longer change.

import type { RunStatus } from './types';

export function revealAllowed(status: RunStatus): boolean {
	return status !== 'active';
}

/** Fields that carry the reveal; none of them may appear in a reply sent while a run is active. */
export const REVEAL_FIELDS = ['spans', 'regimes', 'reveal', 'strategyComparison'] as const;
