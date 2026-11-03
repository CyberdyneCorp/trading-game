<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { pwa } from '#lib/game/pwa.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { session, onHelp, onPatterns }: { session: GameSession; onHelp: () => void; onPatterns: () => void } = $props();
	const s = $derived(session.state);
	const playable = $derived(s ? s.lastDecisionStep - s.warmupBars + 1 : 0);
	const turn = $derived(s ? s.step - s.warmupBars + 1 : 0);
</script>

{#if s}
	<header class="top panel">
		<div class="brand">TRADELAB</div>
		<div class="market">
			<span class="muted">{t('top.market')}</span>
			<strong class="alias">{s.alias}</strong>
			<span class="tag attention" title={t('brief.modeSynthetic', { seed: session.seed })}>{t('top.synthetic')}</span>
			<span class="muted mission">{missionText(s.mission, i18n.locale).title}</span>
			{#if !pwa.online}<span class="tag attention" title={t('pwa.offlineNote')}>{t('pwa.offline')}</span>{/if}
		</div>
		<div class="turn num">
			{t('top.step')} <strong>{Math.max(0, turn)}</strong> {t('top.of')} {playable} · 1h
		</div>
		<div class="actions">
			<button class="primary" onclick={() => session.advance(1)} disabled={session.busy || s.status !== 'active'}>
				▶ {t('top.advance')} <kbd>Space</kbd>
			</button>
			<button onclick={() => session.advance(5)} disabled={session.busy || s.status !== 'active'}>
				▶▶ {t('top.advance5')} <kbd>⇧Space</kbd>
			</button>
			<button onclick={onPatterns} title="P">{t('patterns.open')} <kbd>P</kbd></button>
			<button onclick={onHelp} aria-label={t('top.help')}>?</button>
			<button onclick={() => session.backToMenu()}>{t('top.menu')}</button>
		</div>
	</header>
{/if}

<style>
	.top {
		display: flex;
		align-items: center;
		gap: 1.2rem;
		flex-wrap: wrap;
		padding: 0.4rem 0.8rem;
	}
	.brand {
		font-family: var(--font-pixel);
		font-size: 1.8rem;
		color: var(--accent);
		text-shadow: 0 0 8px rgba(57, 255, 136, 0.5);
	}
	.market {
		display: flex;
		gap: 0.5rem;
		align-items: baseline;
		flex-wrap: wrap;
	}
	.alias {
		font-family: var(--font-pixel);
		font-size: 1.6rem;
		color: var(--info);
	}
	.mission {
		font-size: 0.85rem;
	}
	.turn {
		margin-left: auto;
	}
	.actions {
		display: flex;
		gap: 0.4rem;
	}
	kbd {
		font-size: 0.7rem;
		opacity: 0.7;
		margin-left: 0.3em;
	}
</style>
