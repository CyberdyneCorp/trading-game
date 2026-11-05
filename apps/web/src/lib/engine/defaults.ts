// Hand-written values that go with the generated engine types (types.ts is generated from engine/idl).

import type { RunOptionsView } from './types';

/** A run with no advisor allowance and no intel perks: the mission's own rules. */
export const DEFAULT_RUN_OPTIONS: RunOptionsView = { adviceAllowance: 0, newsLead: 0, calendar: false };
