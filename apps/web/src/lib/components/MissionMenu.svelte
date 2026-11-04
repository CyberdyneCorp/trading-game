<script lang="ts">
	import { sound } from '#lib/game/sound.svelte.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { settings } from '#lib/game/settings.svelte.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { i18n, t, type Locale } from '#lib/game/i18n.svelte.ts';

	let { session }: { session: GameSession } = $props();

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
