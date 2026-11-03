<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { session }: { session: GameSession } = $props();
	let pct = $state(50);
</script>

<div class="modal-backdrop" role="presentation">
	<div class="modal" role="dialog" aria-modal="true" aria-labelledby="cal-title">
		<h2 id="cal-title">{t('cal.title')}</h2>
		<p class="q">
			{t('cal.question', { h: session.state?.calibration.horizon ?? 5, price: fmtPrice(session.state?.account.mark, i18n.locale) })}
		</p>
		<label>
			<span class="big">{pct}%</span>
			<input type="range" min="5" max="95" step="5" bind:value={pct} aria-label="%" />
		</label>
		<p class="muted small">{t('cal.why')}</p>
		<div class="actions">
			<button onclick={() => session.answerQuestion(null)}>{t('cal.skip')}</button>
			<button class="primary" onclick={() => session.answerQuestion(pct / 100)}>{t('cal.answer')}</button>
		</div>
	</div>
</div>

<style>
	.q {
		font-size: 1.05rem;
	}
	label {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.3rem;
	}
	.big {
		font-family: var(--font-pixel);
		font-size: 2.6rem;
		color: var(--info);
	}
	.small {
		font-size: 0.8rem;
	}
	.actions {
		display: flex;
		justify-content: flex-end;
		gap: 0.5rem;
	}
</style>
