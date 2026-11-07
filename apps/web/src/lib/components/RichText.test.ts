import { render } from 'svelte/server';
import { describe, expect, it } from 'vitest';
import RichText from './RichText.svelte';

/** The visible text of server-rendered markup: tags, comments and hidden elements removed. */
function visibleText(html: string): string {
	return html
		.replace(/<!--[\s\S]*?-->/g, '')
		.replace(/<span[^>]*hidden[^>]*>[^<]*<\/span>/g, '')
		.replace(/<[^>]+>/g, '');
}

describe('RichText glossary links', () => {
	it('a linked term adds no space around itself inside a sentence', () => {
		const { body } = render(RichText, { props: { text: 'Uma [[call|call]] dá o direito de comprar ao [[strike|strike]]; o ([[writing|lançamento]]).' } });
		expect(visibleText(body)).toBe('Uma call dá o direito de comprar ao strike; o (lançamento).');
	});
});
