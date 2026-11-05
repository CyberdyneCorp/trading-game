<script lang="ts">
	import { sound } from '#lib/game/sound.svelte.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { settings } from '#lib/game/settings.svelte.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { i18n, packReasonText, t, type Locale } from '#lib/game/i18n.svelte.ts';
	import { PACK_MAX_BYTES } from '#lib/game/packs.ts';
	import type { StoredPack } from '#lib/game/storage.ts';

	let { session }: { session: GameSession } = $props();
	let packMessage = $state<{ ok: boolean; text: string } | null>(null);

	async function onPackFile(e: Event & { currentTarget: HTMLInputElement }) {
		const input = e.currentTarget;
		const file = input.files?.[0];
		input.value = '';
		if (!file) return;
		if (file.size > PACK_MAX_BYTES) {
			packMessage = { ok: false, text: t('pack.refused', { reason: packReasonText('too_large') }) };
			return;
		}
		const label = file.name.replace(/\.tlpack$/, '');
		const check = await session.installPack(new Uint8Array(await file.arrayBuffer()), label);
		packMessage = check.ok
			? { ok: true, text: t('pack.installed', { label }) }
			: { ok: false, text: t('pack.refused', { reason: packReasonText(check.reason) }) };
	}

	function exportPack(p: StoredPack) {
		const url = URL.createObjectURL(new Blob([p.bytes as BlobPart], { type: 'application/octet-stream' }));
		const a = document.createElement('a');
		a.href = url;
		a.download = `${p.scenarioId}.tlpack`;
		a.click();
		URL.revokeObjectURL(url);
	}

	function setLocale(l: Locale) {
		i18n.locale = l;
		settings.save();
	}
</script>

<main class="menu">
	<header>
		<h1 class="glow">TRADELAB</h1>
		<p class="muted">{t('app.subtitle')}</p>
	</header>

	{#if pwa.updateReady}
		<section class="panel banner info" role="status">
			<span>⟳ {t('pwa.update')}</span>
			<button class="primary" onclick={() => pwa.apply()}>{t('pwa.apply')}</button>
		</section>
	{/if}
	{#if !pwa.online}
		<p class="panel banner attention" role="status"><span class="tag attention">{t('pwa.offline')}</span> {t('pwa.offlineNote')}</p>
	{/if}
	{#if !session.durable}
		<p class="panel banner down" role="alert">⚠ {t('pwa.notDurable')}</p>
	{/if}
	{#if pwa.iosHint}
		<p class="panel banner muted">{t('pwa.iosHint')}</p>
	{/if}

	{#if session.menuAlert}
		<p class="panel banner down" role="alert" data-testid="menu-alert">⚠ {session.menuAlert}</p>
	{/if}

	{#if session.saved}
		<section class="panel resume">
			<span>{missionText(session.saved.missionId, i18n.locale).title} · seed {session.saved.seed}</span>
			<button class="primary" onclick={() => session.resume()} disabled={session.busy}>▶ {t('menu.resume')}</button>
		</section>
	{/if}

	<h2>{t('menu.missions')}</h2>
	<div class="cards">
		{#each session.missions as m (m.id)}
			{@const txt = missionText(m.id, i18n.locale)}
			{@const done = session.completed.includes(m.id)}
			<article class="panel card" class:done data-testid="mission-card">
				<h3>
					{txt.title}
					{#if done}<span class="tag up completed">✓ {t('menu.completed')}</span>{/if}
				</h3>
				<p>{txt.tagline}</p>
				<p class="muted small">{txt.topic}</p>
				<p class="small">
					<span class="tag info">{t('menu.leverageCap')} {m.leverageCap}x</span>
					<span class="tag muted">{m.totalBars - m.warmupBars} {t('menu.bars')}</span>
					<span class="tag attention">{t('top.synthetic')}</span>
				</p>
				<button class="primary" onclick={() => session.startMission(m.id)} disabled={session.busy}>{t('menu.start')}</button>
			</article>
		{/each}
	</div>

	<h2>{t('menu.practice')}</h2>
	<section class="panel practice" data-testid="practice">
		<p class="muted small">{t('menu.practiceNote')}</p>
		{#if session.packs.length === 0}<p class="muted">{t('menu.noPacks')}</p>{/if}
		<ul class="packs">
			{#each session.packs as p (p.digest)}
				<li data-testid="pack-row">
					<span class="pack-name">{p.label}</span>
					<span class="tag attention">{t('menu.known')}</span>
					<span class="tag muted">{p.totalBars - p.warmupBars} {t('menu.bars')}</span>
					<span class="tag info">{t('menu.leverageCap')} {p.leverageCap}x</span>
					{#if p.gaps > 0}<span class="tag muted">{t('menu.gaps', { n: p.gaps })}</span>{/if}
					<span class="pack-actions">
						<button onclick={() => exportPack(p)}>{t('menu.exportPack')}</button>
						<button onclick={() => session.removePack(p.digest)} disabled={session.busy}>{t('menu.deletePack')}</button>
						<button class="primary" onclick={() => session.startPack(p.digest)} disabled={session.busy} data-testid="pack-start">
							▶ {t('menu.startPack')}
						</button>
					</span>
				</li>
			{/each}
		</ul>
		<div class="pack-buttons">
			<button onclick={() => session.openImport()} data-testid="import-csv">{t('menu.importCsv')}</button>
			<label class="file-button">
				{t('menu.loadPack')}
				<input type="file" accept=".tlpack,application/octet-stream" onchange={onPackFile} data-testid="pack-file" />
			</label>
		</div>
		{#if packMessage}<p class={packMessage.ok ? 'up' : 'down'} role="status" data-testid="pack-message">{packMessage.text}</p>{/if}
	</section>

	<section class="panel settings">
		<h2 class="panel-title">{t('menu.settings')}</h2>
		<label><input type="checkbox" bind:checked={settings.crt} onchange={() => settings.save()} /> {t('settings.crt')}</label>
		<label><input type="checkbox" bind:checked={settings.colorblind} onchange={() => settings.save()} /> {t('settings.palette')}</label>
		<label>
			<input type="checkbox" checked={!sound.muted} onchange={(e) => sound.setMuted(!e.currentTarget.checked)} data-testid="sound-on" />
			{t('sound.on')}
		</label>
		<label class="volume">
			{t('sound.music')}
			<input type="range" min="0" max="1" step="0.05" value={sound.musicVolume} oninput={(e) => sound.setMusicVolume(Number(e.currentTarget.value))} />
		</label>
		<label class="volume">
			{t('sound.effects')}
			<input type="range" min="0" max="1" step="0.05" value={sound.sfxVolume} oninput={(e) => sound.setSfxVolume(Number(e.currentTarget.value))} />
		</label>
		{#if session.persisted !== null}
			<span class="muted">{t('pwa.storage')}: {session.persisted ? t('pwa.persisted') : t('pwa.notPersisted')}</span>
		{/if}
		<span>
			{t('settings.language')}:
			<button class:primary={i18n.locale === 'pt-BR'} onclick={() => setLocale('pt-BR')}>PT-BR</button>
			<button class:primary={i18n.locale === 'en'} onclick={() => setLocale('en')}>EN</button>
		</span>
	</section>

	<p class="muted small disclaimer">{t('app.disclaimer')}</p>
</main>

<style>
	.menu {
		max-width: 1100px;
		margin: 0 auto;
		padding: 2rem 1rem;
	}
	.glow {
		font-size: 4.5rem;
		text-shadow:
			0 0 10px rgba(57, 255, 136, 0.6),
			0 0 30px rgba(57, 255, 136, 0.25);
		margin: 0;
	}
	.cards {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
		gap: 0.8rem;
	}
	.card {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	.card h3 {
		display: flex;
		justify-content: space-between;
		align-items: baseline;
		gap: 0.5rem;
	}
	.completed {
		flex: none;
		color: var(--up);
	}
	.card.done {
		border-color: var(--up);
	}
	.card button {
		margin-top: auto;
	}
	.banner {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 1rem;
		margin: 0.6rem 0;
	}
	.resume {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 1rem;
		margin: 1rem 0;
	}
	.packs {
		list-style: none;
		padding: 0;
		margin: 0.5rem 0;
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	.packs li {
		display: flex;
		flex-wrap: wrap;
		align-items: center;
		gap: 0.4rem;
	}
	.pack-name {
		font-weight: bold;
	}
	.pack-actions {
		margin-left: auto;
		display: flex;
		gap: 0.3rem;
	}
	.pack-buttons {
		display: flex;
		gap: 0.6rem;
		align-items: center;
	}
	.file-button {
		cursor: pointer;
		border: 1px solid var(--line);
		border-radius: var(--radius);
		padding: 0.3rem 0.6rem;
	}
	.file-button input {
		display: none;
	}
	.settings {
		margin-top: 1.2rem;
		display: flex;
		gap: 1.5rem;
		flex-wrap: wrap;
		align-items: center;
	}
	.settings label {
		display: flex;
		gap: 0.4rem;
		align-items: center;
	}
	.settings input[type='checkbox'] {
		width: auto;
	}
	.small {
		font-size: 0.8rem;
	}
	.disclaimer {
		margin-top: 1.5rem;
	}
	p {
		margin: 0;
	}
</style>
