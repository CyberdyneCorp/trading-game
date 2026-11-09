// The shared glossary (design D7): one data file read by every client. Each term names the stage that
// teaches it, its related terms and, per locale, the term, its aliases, a short definition (shown in
// place) and a longer explanation, with an optional worked example whose numbers are `checks`.
// Lesson and tutorial texts link terms with [[id|shown text]] (or [[id]] for the term's own name).

import type { FindingCode } from '#lib/engine/types.ts';
import type { StageId } from '#lib/game/curriculum.ts';
import type { Locale } from '#lib/game/i18n.svelte.ts';
import type { Check } from './checks.ts';
import glossaryJson from './glossary.json';

export interface TermText {
	term: string;
	aliases: string[];
	short: string;
	long: string;
	example?: string;
}

export interface GlossaryTerm {
	id: string;
	stage: StageId;
	related: string[];
	text: Record<Locale, TermText>;
	checks?: Check[];
}

export const GLOSSARY: readonly GlossaryTerm[] = (glossaryJson as { terms: GlossaryTerm[] }).terms;

const byId = new Map(GLOSSARY.map((t) => [t.id, t]));

export function termById(id: string): GlossaryTerm | null {
	return byId.get(id) ?? null;
}

/** Lowercase without accents, so "liquidacao" finds "Liquidação". */
export function fold(s: string): string {
	return s
		.normalize('NFD')
		.replace(/[̀-ͯ]/g, '')
		.toLowerCase()
		.trim();
}

/** Terms whose name or aliases contain the query (accents and case ignored), alphabetical in the locale. */
export function searchTerms(query: string, locale: Locale): GlossaryTerm[] {
	const q = fold(query);
	const hits = GLOSSARY.filter((t) => {
		const tx = t.text[locale];
		return !q || [tx.term, ...tx.aliases].some((s) => fold(s).includes(q));
	});
	return hits.sort((a, b) => a.text[locale].term.localeCompare(b.text[locale].term, locale, { sensitivity: 'base' }));
}

export type RichSegment = { text: string; term?: undefined } | { text: string; term: string };

const LINK = /\[\[([a-z0-9-]+)(?:\|([^\]]+))?\]\]/g;

/** Splits a text into plain parts and glossary links ([[id|text]] or [[id]]). */
export function parseLinks(text: string, locale: Locale = 'pt-BR'): RichSegment[] {
	const out: RichSegment[] = [];
	let last = 0;
	for (const m of text.matchAll(LINK)) {
		if (m.index > last) out.push({ text: text.slice(last, m.index) });
		out.push({ term: m[1], text: m[2] ?? termById(m[1])?.text[locale].term ?? m[1] });
		last = m.index + m[0].length;
	}
	if (last < text.length) out.push({ text: text.slice(last) });
	return out;
}

/** The text without link markup (what a reader sees). */
export function plainText(text: string, locale: Locale = 'pt-BR'): string {
	return parseLinks(text, locale)
		.map((s) => s.text)
		.join('');
}

/** The glossary term a debrief finding is about (findings without one show no term). */
export const FINDING_TERMS: Partial<Record<FindingCode, string>> = {
	liquidated: 'liquidation-price',
	stop_beyond_liq: 'liquidation-price',
	no_stop: 'stop-loss',
	gap_slippage: 'gap',
	funding_cost: 'funding-rate',
	capacity: 'capacity',
	good_process_loss: 'expectancy',
	good_process_win: 'expectancy',
	lucky_win: 'journal',
	no_plan: 'thesis',
	oversized: 'risk-budget',
	bankrupt: 'risk-of-ruin',
	margin_called: 'margin-call',
	theta_decay: 'theta',
	iv_crush: 'iv-crush',
	iv_rise: 'vega',
	wrote_options: 'writing',
	leverage_vs_stop: 'leverage',
	liq_buffer: 'liquidation-price',
	drawdown_budget: 'drawdown',
	revenge_trade: 'revenge-trading',
	fomo_entry: 'fomo',
	averaging_down: 'averaging-down',
	risk_limit_exceeded: 'risk-limit'
};

/** Term ids linked in a text. */
export function linkedTerms(text: string): string[] {
	return [...text.matchAll(LINK)].map((m) => m[1]);
}
