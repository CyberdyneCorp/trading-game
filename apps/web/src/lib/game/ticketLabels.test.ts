import { afterEach, describe, expect, it } from 'vitest';
import { i18n, t, type Locale } from './i18n.svelte.ts';

// The "size by" select sits in half of the ticket (about 150px at 1024px wide): its options name only
// what the order is sized by, and the amount field beside it (which may wrap) carries the unit.
const OPTIONS = ['ticket.sizeRisk', 'ticket.sizeQty', 'ticket.sizeNotional', 'ticket.sizeEquity'] as const;

describe('order ticket labels', () => {
	afterEach(() => (i18n.locale = 'pt-BR'));
	it.each(['pt-BR', 'en'] as Locale[])('regression: %s "size by" options fit the half-width select (were clipped)', (l) => {
		i18n.locale = l;
		for (const key of OPTIONS) expect(t(key).length, key).toBeLessThanOrEqual(12);
		expect(t('ticket.riskPct')).toContain('%');
		expect(t('ticket.equityPct')).toContain('%');
	});
});
