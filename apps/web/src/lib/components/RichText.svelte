<script lang="ts">
	// A lesson or tutorial text with its glossary links ([[id|text]]) shown as in-place definitions.
	import { parseLinks } from '#lib/content/glossary.ts';
	import { i18n } from '#lib/game/i18n.svelte.ts';
	import Term from './Term.svelte';

	let { text }: { text: string } = $props();
	const parts = $derived(parseLinks(text, i18n.locale));
</script>

{#each parts as p, k (k)}{#if p.term}<Term id={p.term}>{p.text}</Term>{:else}{p.text}{/if}{/each}
