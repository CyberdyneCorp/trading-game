<script lang="ts">
	// Pack catalog (web-client tasks 4.4, 5.3, 5.5): browse published packs, download them with
	// resume, play installed ones. Offline or unavailable, the last catalog seen stays listed with its
	// downloads disabled; installed packs remain playable.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtBytes, localized, type CatalogEntry } from '#lib/game/catalog.ts';
	import { fmtNum } from '#lib/game/format.ts';
	import type { DownloadView } from '#lib/game/catalog.svelte.ts';
	import { intervalLabel } from '#lib/game/packs.ts';
	import { marketTagKey } from '#lib/game/missions.ts';
	import { i18n, packReasonText, t, type MessageKey } from '#lib/game/i18n.svelte.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';

	let { session }: { session: GameSession } = $props();
	const cat = $derived(session.catalog);
	const L = $derived(i18n.locale);
	const canDownload = $derived(cat.status === 'ready' && pwa.online);
	const size = (n: number) => fmtBytes(n, L);

	function stateText(d: DownloadView): string {
		const p = { received: size(d.received), size: size(d.size) };
		if (d.phase === 'downloading') return t('catalog.progress', p);
		if (d.phase === 'background') return t('catalog.background');
		if (d.phase === 'verifying') return t('catalog.verifying');
		if (d.phase === 'noSpace') return t('catalog.noSpace', { need: size(d.shortfall ?? 0) });
		if (d.phase === 'refused') return t('pack.refused', { reason: packReasonText(d.reason ?? '') });
		return t(`catalog.failed.${d.reason ?? 'interrupted'}` as MessageKey, p);
	}

	const tone = (d: DownloadView) => (d.phase === 'downloading' || d.phase === 'background' || d.phase === 'verifying' ? 'info' : 'down');
	const play = (e: CatalogEntry) => session.startPack(e.digest);
	const download = (e: CatalogEntry) => cat.download(e, localized(e.title, L));
</script>

<main class="catalog">
	<header>
		<h1>{t('catalog.title')}</h1>
		<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
	</header>
	<p class="muted">{t('catalog.note')}</p>
	{#if cat.quota}
		<p class="muted small" data-testid="catalog-quota">{t('catalog.quota', { used: size(cat.quota.usage), total: size(cat.quota.quota) })}</p>
	{/if}

	{#if cat.status === 'loading' || cat.status === 'idle'}
		<p class="muted" role="status">{t('catalog.loading')}</p>
	{:else if cat.status === 'offline'}
		<p class="panel banner attention" role="status" data-testid="catalog-state">
			<span class="tag attention">{t('pwa.offline')}</span>
			{t('catalog.offline')}
		</p>
	{:else if cat.status === 'unavailable'}
		<p class="panel banner down" role="status" data-testid="catalog-state">
			{t('catalog.unavailable')}
			<button onclick={() => cat.load()} data-testid="catalog-retry">{t('catalog.retry')}</button>
		</p>
	{/if}
	{#if cat.status !== 'ready' && cat.status !== 'loading' && cat.entries.length > 0}
		<p class="muted small">{t('catalog.cached')}</p>
	{/if}
	{#if cat.status === 'ready' && cat.entries.length === 0}
		<p class="muted">{t('catalog.empty')}</p>
	{/if}

	<ul class="entries">
		{#each cat.entries as e (e.digest)}
			{@const st = cat.statusOf(e)}
			{@const d = cat.downloads[e.digest]}
			<li class="panel entry" data-testid="catalog-entry" data-status={st}>
				<div class="head">
					<strong>{localized(e.title, L)}</strong>
					{#if st === 'installed'}<span class="tag up" data-testid="catalog-installed">{t('catalog.installed')}</span>{/if}
					<span class="tag attention" data-testid="catalog-tag">{t(marketTagKey(true, session.packs.find((p) => p.digest === e.digest)?.mode ?? e.mode))}</span>
				</div>
				{#if localized(e.summary, L)}<p>{localized(e.summary, L)}</p>{/if}
				<p class="small">
					<span class="tag muted">{intervalLabel(e.intervalSeconds)}</span>
					<span class="tag muted">{fmtNum(e.bars, L, 0)} {t('menu.bars')}</span>
					<span class="tag info">{t('menu.leverageCap')} {e.leverageCap}x</span>
					<span class="tag muted">{size(e.size)}</span>
				</p>
				{#if d}
					<p class={tone(d)} role="status" data-testid="download-state">{stateText(d)}</p>
					{#if d.phase === 'downloading'}
						<progress max={d.size} value={d.received} data-testid="download-progress"></progress>
					{/if}
				{/if}
				<div class="actions">
					{#if st === 'installed'}
						<button class="primary" onclick={() => play(e)} disabled={session.busy} data-testid="catalog-play">▶ {t('menu.startPack')}</button>
					{:else if !canDownload}
						<span class="muted" data-testid="catalog-needs-connection">{t('catalog.needsConnection')}</span>
					{:else if d?.phase === 'downloading'}
						<button onclick={() => cat.pause(e)} data-testid="catalog-pause">{t('catalog.pause')}</button>
					{:else if st === 'partial'}
						<button class="primary" onclick={() => download(e)} disabled={cat.busy(e)} data-testid="catalog-resume">
							{t('catalog.resume', { received: size(cat.partial.get(e.digest) ?? 0), size: size(e.size) })}
						</button>
					{:else}
						<button class="primary" onclick={() => download(e)} disabled={cat.busy(e)} data-testid="catalog-download">
							{t('catalog.download', { size: size(e.size) })}
						</button>
					{/if}
				</div>
			</li>
		{/each}
	</ul>
</main>

<style>
	.catalog {
		max-width: 900px;
		margin: 0 auto;
		padding: 2rem 1rem;
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 1rem;
	}
	h1,
	p {
		margin: 0;
	}
	.banner {
		display: flex;
		align-items: center;
		gap: 0.8rem;
		flex-wrap: wrap;
	}
	.entries {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}
	.entry {
		display: flex;
		flex-direction: column;
		gap: 0.35rem;
	}
	.head {
		display: flex;
		gap: 0.5rem;
		align-items: baseline;
		flex-wrap: wrap;
	}
	.actions {
		display: flex;
		gap: 0.4rem;
		justify-content: flex-end;
	}
	progress {
		width: 100%;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
