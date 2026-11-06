<script lang="ts">
	import { keymap } from '#lib/game/keymap.svelte.ts';
	import { keyLabel } from '#lib/game/shortcuts.ts';
	import { sound } from '#lib/game/sound.svelte.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { marketTagKey, missionText, packModeKey } from '#lib/game/missions.ts';
	import { campaignMissions } from '#lib/game/curriculum.ts';
	import { branchTurn, intervalLabel } from '#lib/game/packs.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { AUTO_SPEEDS, type AutoPlay } from '#lib/game/autoplay.svelte.ts';

	let {
		session,
		auto,
		onHelp,
		onPatterns
	}: { session: GameSession; auto: AutoPlay; onHelp: () => void; onPatterns: () => void } = $props();
	const s = $derived(session.state);
	const playable = $derived(s ? s.lastDecisionStep - s.warmupBars + 1 : 0);
	const turn = $derived(s ? s.step - s.warmupBars + 1 : 0);
	const missionTitle = $derived(s ? missionText(s.mission, i18n.locale, session.runPackOrigin, session.runPackTexts).title : '');
	const branch = $derived(s ? branchTurn(s) : null);
	const modeLine = $derived(
		s ? t(s.knownScenario ? packModeKey(s.marketMode, session.runPackOrigin) : 'brief.modeSynthetic', { seed: session.seed }) : ''
	);
</script>

{#if s}
	<header class="top panel">
		<div class="brand">TRADELAB</div>
		<div class="market">
			<span class="muted">{t('top.market')}</span>
			<strong class="alias">{s.alias}</strong>
			<span class="tag attention" title={modeLine} data-testid="market-tag">{t(marketTagKey(s.knownScenario, s.marketMode))}</span>
			{#if session.runAssisted}
				<span class="tag attention" title={t('assist.banner')} data-testid="assisted-tag">{t('assist.tag')}</span>
			{/if}
			{#if branch !== null}
				<span class="tag attention" title={t('top.branchNote', { bar: branch })} data-testid="branch-tag">{t('top.branch')}</span>
			{/if}
			{#if session.runCampaign}
				<span class="tag info" data-testid="campaign-tag">{t('campaign.tag', { n: session.runCampaign.index + 1, total: campaignMissions().length })}</span>
			{/if}
			<span class="muted mission" title={missionTitle}>{missionTitle}</span>
			{#if !pwa.online}<span class="tag attention" title={t('pwa.offlineNote')}>{t('pwa.offline')}</span>{/if}
		</div>
		<div class="turn num">
			{t('top.step')} <strong>{Math.max(0, turn)}</strong> {t('top.of')} {playable} · {intervalLabel(s.stepSeconds)}
		</div>
		<div class="actions">
			<button class="primary" onclick={() => session.advance(1)} disabled={session.busy || s.status !== 'active'}>
				▶ {t('top.advance')} <kbd>{keyLabel(keymap.map.advance)}</kbd>
			</button>
			<div class="auto" role="group" aria-label={t('auto.title')}>
				<button
					class:running={auto.running}
					onclick={() => auto.toggle()}
					disabled={s.status !== 'active'}
					aria-pressed={auto.running}
					data-testid="auto-toggle"
				>
					{auto.running ? '⏸' : '⏵'} {t(auto.running ? 'auto.pause' : 'auto.play')} <kbd>{keyLabel(keymap.map.auto)}</kbd>
				</button>
				<select
					aria-label={t('auto.speed')}
					value={auto.speed}
					onchange={(e) => auto.setSpeed(Number((e.currentTarget as HTMLSelectElement).value))}
				>
					{#each AUTO_SPEEDS as sp, i (sp.label)}<option value={i}>{sp.label}</option>{/each}
				</select>
				<label class="small" title={t('auto.pauseOnEventsHint')}>
					<input type="checkbox" bind:checked={auto.pauseOnEvents} />
					{t('auto.pauseOnEvents')}
				</label>
			</div>
			<button onclick={onPatterns} title={keyLabel(keymap.map.patterns)}>{t('patterns.open')} <kbd>{keyLabel(keymap.map.patterns)}</kbd></button>
			<button
				onclick={() => sound.toggleMute()}
				aria-label={sound.muted ? t('sound.unmute') : t('sound.mute')}
				aria-pressed={!sound.muted}
				title="M"
				data-testid="sound-toggle">{sound.muted ? '♪✕' : '♪'}</button
			>
			<button onclick={onHelp} aria-label={t('top.help')}>?</button>
			<button onclick={() => session.backToMenu()}>{t('top.menu')}</button>
		</div>
	</header>
{/if}

<style>
	.top {
		display: flex;
		align-items: center;
		gap: 0.8rem;
		flex-wrap: wrap;
		padding: 0.4rem 0.8rem;
	}
	.brand {
		font-family: var(--font-pixel);
		font-size: 1.8rem;
		color: var(--accent);
		text-shadow: 0 0 8px rgba(57, 255, 136, 0.5);
	}
	/* The market/mission block shrinks first (the mission title truncates), so the controls stay on
	   one row on desktop instead of wrapping and shrinking the chart. */
	.market {
		display: flex;
		gap: 0.5rem;
		align-items: baseline;
		flex: 1 1 0;
		min-width: 8rem;
		overflow: hidden;
		white-space: nowrap;
	}
	.alias {
		font-family: var(--font-pixel);
		font-size: 1.6rem;
		color: var(--info);
	}
	.mission {
		font-size: 0.85rem;
		overflow: hidden;
		text-overflow: ellipsis;
		min-width: 0;
	}
	.turn {
		white-space: nowrap;
	}
	.actions {
		display: flex;
		flex-wrap: wrap;
		gap: 0.4rem;
	}
	kbd {
		font-size: 0.7rem;
		opacity: 0.7;
		margin-left: 0.3em;
	}
	.auto {
		display: inline-flex;
		align-items: center;
		gap: 0.3rem;
		border: 1px solid var(--line);
		padding: 0.15rem 0.3rem;
	}
	.auto .running {
		background: rgba(255, 200, 87, 0.15);
		border-color: var(--attention);
		color: var(--attention);
	}
	.auto select {
		width: auto;
		min-height: 0;
		padding: 0 0.3rem;
	}
	.auto .small {
		font-size: 0.72rem;
		color: var(--muted);
		display: inline-flex;
		gap: 0.2rem;
		align-items: center;
	}
	.auto .small input {
		width: auto;
	}
</style>
