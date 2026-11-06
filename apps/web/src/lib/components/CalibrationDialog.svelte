<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { lastPrice } from '#lib/game/lastPrice.ts';

	let { session }: { session: GameSession } = $props();
	let pct = $state(50);
	let modal: HTMLDivElement | undefined = $state();

	// Modal: Tab cycles inside the dialog instead of reaching the terminal behind it.
	function keepFocus(e: KeyboardEvent) {
		if (e.key !== 'Tab' || !modal) return;
		const items = Array.from(modal.querySelectorAll<HTMLElement>('input, button'));
		const edge = e.shiftKey ? items[0] : items[items.length - 1];
		if (document.activeElement !== edge) return;
		e.preventDefault();
		(e.shiftKey ? items[items.length - 1] : items[0]).focus();
	}
</script>

<div class="modal-backdrop" role="presentation">
	<div class="modal" role="dialog" aria-modal="true" aria-labelledby="cal-title" tabindex="-1" bind:this={modal} onkeydown={keepFocus}>
		<h2 id="cal-title">{t('cal.title')}</h2>
		<p class="q" id="cal-q">
			{t('cal.question', { h: session.state?.calibration.horizon ?? 5, price: session.state ? fmtPrice(String(lastPrice(session.state)), i18n.locale) : '' })}
		</p>
		<label>
			<span class="big">{pct}%</span>
			<!-- Focus starts on the slider, so the keyboard does not have to cross the terminal behind. -->
			<!-- svelte-ignore a11y_autofocus -->
			<input type="range" min="5" max="95" step="5" bind:value={pct} aria-labelledby="cal-q" aria-valuetext="{pct}%" autofocus />
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
