import { describe, expect, it } from 'vitest';
import { STAGES } from '#lib/game/curriculum.ts';
import { tutorialFor } from '#lib/game/tutorials.ts';
import { LOCALES } from './academy.ts';
import { checkProblem } from './checks.ts';
import { FINDING_TERMS, fold, GLOSSARY, linkedTerms, parseLinks, plainText, searchTerms, termById } from './glossary.ts';

const COMPONENTS = import.meta.glob<string>('../components/*.svelte', { query: '?raw', import: 'default', eager: true });

describe('glossary (design D7)', () => {
	it('has about 70 terms with unique ids, a known stage and existing related terms', () => {
		expect(GLOSSARY.length).toBeGreaterThanOrEqual(65);
		expect(new Set(GLOSSARY.map((t) => t.id)).size).toBe(GLOSSARY.length);
		const stages = new Set(STAGES.map((s) => s.id));
		for (const t of GLOSSARY) {
			expect(stages.has(t.stage), t.id).toBe(true);
			for (const r of t.related) expect(termById(r), `${t.id} → ${r}`).not.toBeNull();
		}
	});

	it('every term has a name, a short definition of at most two sentences and a longer text in both locales', () => {
		for (const t of GLOSSARY)
			for (const locale of LOCALES) {
				const tx = t.text[locale];
				expect(tx.term && tx.short && tx.long, `${t.id} ${locale}`).toBeTruthy();
				expect(Array.isArray(tx.aliases)).toBe(true);
				expect(tx.short.split(/(?<=[.!?])\s+/).length, `${t.id} ${locale}`).toBeLessThanOrEqual(2);
				expect(!!tx.example).toBe(!!t.text[locale === 'en' ? 'pt-BR' : 'en'].example);
			}
	});

	it('worked examples are verified like lesson numbers', () => {
		for (const t of GLOSSARY) {
			if (t.text.en.example) expect(t.checks?.length, t.id).toBeGreaterThan(0);
			for (const c of t.checks ?? []) expect(checkProblem(c), t.id).toBeNull();
		}
	});

	it('Search without accents: the PT-BR player searching "liquidacao" finds "Liquidação"', () => {
		const hits = searchTerms('liquidacao', 'pt-BR').map((t) => t.text['pt-BR'].term);
		expect(hits).toContain('Preço de liquidação');
		expect(searchTerms('LIQUIDATION', 'en').map((t) => t.id)).toContain('liquidation-price');
		expect(searchTerms('ATM', 'en').map((t) => t.id)).toEqual(['moneyness']); // an alias
		expect(fold('Ação')).toBe('acao');
	});

	it('lists terms alphabetically in the locale', () => {
		const names = searchTerms('', 'pt-BR').map((t) => t.text['pt-BR'].term);
		expect(names).toHaveLength(GLOSSARY.length);
		expect([...names].sort((a, b) => a.localeCompare(b, 'pt-BR', { sensitivity: 'base' }))).toEqual(names);
	});

	it('parses [[id|text]] and [[id]] links and strips them to plain text', () => {
		expect(parseLinks('Um [[theta|theta alto]] custa.')).toEqual([{ text: 'Um ' }, { term: 'theta', text: 'theta alto' }, { text: ' custa.' }]);
		expect(plainText('O [[delta]] mede.', 'en')).toBe('O Delta mede.');
		expect(linkedTerms('[[gap]] e [[spread|o spread]]')).toEqual(['gap', 'spread']);
	});

	it('Linked term missing: every term marked in the UI or linked by a tutorial exists in both locales', () => {
		const marked = Object.values(COMPONENTS).flatMap((src) => [...src.matchAll(/<Term id="([a-z-]+)"/g)].map((m) => m[1]));
		expect(marked.length).toBeGreaterThan(15);
		const tutorialLinks = ['m01-false-breakout', 'm04-options-buyer', 'm05-options-writer', 'm10-hedged-book'].flatMap((m) =>
			LOCALES.flatMap((l) => tutorialFor(m, l).flatMap((c) => linkedTerms(`${c.title} ${c.body}`)))
		);
		expect(tutorialLinks.length).toBeGreaterThan(0);
		for (const id of [...marked, ...tutorialLinks, ...Object.values(FINDING_TERMS)])
			for (const locale of LOCALES) expect(termById(id)?.text[locale].short, id).toBeTruthy();
	});
});
