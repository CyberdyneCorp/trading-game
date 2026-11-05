<script lang="ts">
	// Released news, plus what an intel perk delivers: headlines a fast wire shows before their public
	// release (flagged early) and the economic calendar's bars of coming rate decisions (bar only).
	// The engine never serializes anything else about the future.
	import type { NewsView } from '#lib/engine/types.ts';
	import { headline, level } from '#lib/game/news.ts';
	import { i18n, t, tCount } from '#lib/game/i18n.svelte.ts';

	let { news, calendar = [], step }: { news: NewsView[]; calendar?: number[]; step: number } = $props();
	const items = $derived(news.slice().reverse());
</script>

<section class="panel news" aria-label={t('news.title')}>
	<h2 class="panel-title">{t('news.title')} <span class="tag attention">{t('news.fictional')}</span></h2>
	{#if calendar.length}
		<ul class="calendar" data-testid="news-calendar">
			{#each calendar as at (at)}
				<li>📅 {t('news.calendarItem', { step: at })} <span class="muted">· {tCount('news.inBars', at - step)}</span></li>
			{/each}
		</ul>
	{/if}
	{#if items.length === 0}
		<p class="muted small">{t('news.none')}</p>
	{:else}
		<ol aria-live="polite">
			{#each items as n (n.step + n.kind)}
				<li class:early={n.early} data-testid={n.early ? 'news-early' : undefined}>
					<span class="muted">[{n.step}]</span>
					{#if n.early}<span class="tag info">{t('news.early')}</span>{/if}
					<strong class={n.tone > 0 ? 'up' : n.tone < 0 ? 'down' : ''}>{headline(n, i18n.locale)}</strong>
					<div class="meta muted">
						{#if n.early}{tCount('news.publicIn', n.step - step)} ·{/if}
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
	ol,
	.calendar {
		list-style: none;
		margin: 0;
		padding: 0;
		font-size: 0.82rem;
	}
	.calendar {
		margin-bottom: 0.3rem;
		color: var(--info);
	}
	li {
		padding: 0.2rem 0;
		border-bottom: 1px solid rgba(36, 65, 58, 0.5);
	}
	li.early {
		border-left: 2px solid var(--info);
		padding-left: 0.3rem;
	}
	.meta {
		font-size: 0.75rem;
	}
	.small {
		font-size: 0.75rem;
		margin: 0.2rem 0 0;
	}
</style>
