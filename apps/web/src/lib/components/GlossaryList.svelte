<script lang="ts">
	// The glossary's searchable list (design D7, glossary spec "Glossary screen"): the current locale's
	// terms alphabetically, search ignoring accents and case, each term's stage and related terms.
	// Shared by the glossary screen and the in-place dialog ("more").
	import { searchTerms, termById, type GlossaryTerm } from '#lib/content/glossary.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { tick } from 'svelte';

	let { focus = null, seen = [], onOpen }: { focus?: string | null; seen?: readonly string[]; onOpen?: (id: string) => void } = $props();

	let query = $state('');
	let selected = $state<string | null>(null);
	const L = $derived(i18n.locale);
	const hits = $derived(searchTerms(query, L));
	const current = $derived<GlossaryTerm | null>(selected ? termById(selected) : null);

	$effect(() => {
		if (focus) void select(focus);
	});

	async function select(id: string) {
		selected = id;
		onOpen?.(id);
		await tick();
		document.getElementById(`gl-${id}`)?.scrollIntoView({ block: 'nearest' });
	}
</script>

<div class="glossary">
	<label class="search">
		{t('glossary.search')}
		<input type="search" bind:value={query} data-testid="glossary-search" />
	</label>
	<p class="muted small" role="status">{t('glossary.count', { n: hits.length })}</p>
	<div class="cols">
		<ul class="list" aria-label={t('glossary.title')}>
			{#each hits as term (term.id)}
				<li id="gl-{term.id}">
					<button
						class:active={selected === term.id}
						aria-current={selected === term.id ? 'true' : undefined}
						onclick={() => select(term.id)}
						data-testid="glossary-term">
						{term.text[L].term}{#if !seen.includes(term.id)}<span class="new"> {t('glossary.new')}</span>{/if}
					</button>
				</li>
			{/each}
		</ul>
		{#if current}
			<article class="entry" aria-labelledby="gl-title" data-testid="glossary-entry">
				<h2 id="gl-title">{current.text[L].term}</h2>
				<p class="tag info">{t(`stage.${current.stage}`)}</p>
				<p><strong>{current.text[L].short}</strong></p>
				<p>{current.text[L].long}</p>
				{#if current.text[L].example}<p class="example">{t('glossary.example')}: {current.text[L].example}</p>{/if}
				{#if current.text[L].aliases.length}<p class="muted small">{t('glossary.aliases')}: {current.text[L].aliases.join(', ')}</p>{/if}
				{#if current.related.length}
					<p class="related">
						{t('glossary.related')}:
						{#each current.related as r (r)}<button class="link" onclick={() => select(r)}>{termById(r)?.text[L].term}</button>{/each}
					</p>
				{/if}
			</article>
		{:else}
			<p class="muted entry">{t('glossary.pick')}</p>
		{/if}
	</div>
</div>

<style>
	.glossary {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}
	.search {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
	}
	.cols {
		display: grid;
		grid-template-columns: minmax(180px, 1fr) 2fr;
		gap: 0.8rem;
		min-height: 0;
	}
	@media (max-width: 640px) {
		.cols {
			grid-template-columns: 1fr;
		}
	}
	.list {
		list-style: none;
		margin: 0;
		padding: 0;
		max-height: 60vh;
		overflow-y: auto;
	}
	.list button {
		width: 100%;
		text-align: left;
		border: none;
		background: none;
		padding: 0.2rem 0.4rem;
	}
	.list button.active {
		color: var(--accent);
		outline: 1px solid var(--accent);
	}
	.new {
		color: var(--info);
		font-size: 0.7rem;
	}
	.entry h2 {
		margin: 0;
	}
	.related {
		display: flex;
		flex-wrap: wrap;
		gap: 0.4rem;
		align-items: baseline;
	}
	.link {
		background: none;
		border: none;
		color: var(--info);
		text-decoration: underline;
		padding: 0;
	}
	.small {
		font-size: 0.8rem;
		margin: 0;
	}
</style>
