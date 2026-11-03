<script lang="ts">
	// Released news only (the engine never serializes future items).
	import type { NewsView } from '#lib/engine/types.ts';
	import { headline, level } from '#lib/game/news.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { news }: { news: NewsView[] } = $props();
	const items = $derived(news.slice().reverse());
</script>

<section class="panel news" aria-label={t('news.title')}>
	<h2 class="panel-title">{t('news.title')} <span class="tag attention">{t('news.fictional')}</span></h2>
	{#if items.length === 0}
		<p class="muted small">{t('news.none')}</p>
	{:else}
		<ol aria-live="polite">
			{#each items as n (n.step + n.kind)}
				<li>
					<span class="muted">[{n.step}]</span>
					<strong class={n.tone > 0 ? 'up' : n.tone < 0 ? 'down' : ''}>{headline(n, i18n.locale)}</strong>
					<div class="meta muted">
						{t('news.expected')}: {level(n.expectation, i18n.locale)} · {t('news.actual')}: {level(n.actual, i18n.locale)} ·
						{t('news.credibility')} {n.credibility}%
					</div>
				</li>
			{/each}
		</ol>
	{/if}
	<p class="muted small">{t('news.note')}</p>
</section>

<style>
	.news {
		overflow: auto;
		max-height: 40%;
		flex-shrink: 0;
	}
	ol {
		list-style: none;
		margin: 0;
		padding: 0;
		font-size: 0.82rem;
	}
	li {
		padding: 0.2rem 0;
		border-bottom: 1px solid rgba(36, 65, 58, 0.5);
	}
	.meta {
		font-size: 0.75rem;
	}
	.small {
		font-size: 0.75rem;
		margin: 0.2rem 0 0;
	}
</style>
