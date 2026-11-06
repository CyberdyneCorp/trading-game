import { describe, expect, it } from 'vitest';
import { marketTagKey, missionText, packOriginOf, packTextsOf } from './missions.ts';

describe('missions texts and tags', () => {
	// Playtest: the menu row and the top bar tagged a generated pack CONHECIDO / KNOWN.
	it('regression: tags generated prices SYNTHETIC and only recorded packs KNOWN', () => {
		expect(marketTagKey(false, 'synthetic')).toBe('top.synthetic'); // a built-in mission
		expect(marketTagKey(true, 'synthetic')).toBe('top.synthetic'); // a synthetic pack
		expect(marketTagKey(true, 'historical')).toBe('top.known');
		expect(marketTagKey(true, 'branch')).toBe('top.known');
		expect(marketTagKey(true, undefined)).toBe('top.known'); // stored before the mode was kept
	});

	// Playtest: a synthetic pack was briefed with its raw scenario id as the title and no tagline.
	it('regression: a pack generated from a mission is briefed from that mission, never by its raw id', () => {
		const pt = missionText('m09-event-straddle-s7', 'pt-BR');
		expect(pt.title).toBe('Treino · Véspera (pacote sintético)');
		expect(pt.tagline).toBe(missionText('m09-event-straddle', 'pt-BR').tagline);
		expect(pt.reveal).toBe('');
		expect(missionText('m09-event-straddle-s7', 'en').title).toMatch(/^Practice · .+ \(synthetic pack\)$/);
		const other = missionText('gen-xyz', 'en');
		expect(other.title).toBe('Practice · synthetic pack');
		expect(other.tagline).not.toBe('');
	});

	// Playtest: the journal and the menu's resume line called a catalog pack run "Treino · mercado importado".
	it('regression: names a pack run by where its pack came from', () => {
		const packs = [
			{ digest: 'a', origin: 'catalog' as const },
			{ digest: 'b', origin: 'import' as const },
			{ digest: 'c' }
		];
		expect(packOriginOf(packs, 'a')).toBe('catalog');
		expect(packOriginOf(packs, 'b')).toBe('import');
		expect(packOriginOf(packs, 'c')).toBe('import');
		expect(packOriginOf(packs, 'gone')).toBe('import');
		expect(packOriginOf(packs, null)).toBeNull();
		expect(missionText('csv-1', 'pt-BR', packOriginOf(packs, 'a')).title).toBe('Treino · pacote do catálogo');
	});

	it('a schema-v2 pack briefs with its own title and briefing in the shown locale', () => {
		const texts = [
			{ locale: 'pt-BR', title: 'Treino · choque de liquidez', briefing: 'Uma formadora de mercado sai do livro.' },
			{ locale: 'en', title: 'Practice · liquidity shock', briefing: 'A market maker leaves the book.' }
		];
		const packs = [{ digest: 'c', origin: 'catalog' as const, texts }, { digest: 'old', origin: 'catalog' as const }];
		const pt = missionText('csv-9', 'pt-BR', 'catalog', packTextsOf(packs, 'c'));
		expect(pt.title).toBe('Treino · choque de liquidez');
		expect(pt.tagline).toBe('Uma formadora de mercado sai do livro.');
		expect(missionText('csv-9', 'en', 'catalog', packTextsOf(packs, 'c')).title).toBe('Practice · liquidity shock');
		// A v1 pack (no texts) and a mission run keep the generic texts.
		expect(packTextsOf(packs, 'old')).toBeUndefined();
		expect(packTextsOf(packs, null)).toBeUndefined();
		expect(missionText('csv-9', 'pt-BR', 'catalog', packTextsOf(packs, 'old')).title).toBe('Treino · pacote do catálogo');
	});
});
