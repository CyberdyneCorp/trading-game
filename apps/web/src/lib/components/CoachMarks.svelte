<script lang="ts">
	// Coach marks of the guided first trade (design D6, guided-first-trade spec), drawn over the real
	// terminal or debrief. The callout is a non-modal dialog labeled by its title; its target (a
	// `data-coach` element) gets an outline and is described by the callout's text. Opening a step
	// moves focus to the callout; when the tour ends, focus returns to the last target. Space and Enter
	// act on the callout's buttons (the terminal ignores keys inside `data-own-keys`), Esc skips the
	// tour, and each step is announced in a polite live region. The game stays usable meanwhile.
	import { onMount, tick } from 'svelte';
	import { FIRST_TRADE } from '#lib/content/coach.ts';
	import { coach } from '#lib/game/coach.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';

	let { session, place }: { session: GameSession; place: 'terminal' | 'debrief' } = $props();

	const TOUR = 'first-trade';
	// The ticket's stop field is the LevelField the terminal already names by test id.
	const selector = (target: string) => (target === 'stop' ? '[data-testid="ticket-stop"]' : `[data-coach="${target}"]`);
	const step = $derived(coach.step);
	// A calibration question is modal: the mark waits for it, then takes focus.
	const visible = $derived(!!step && (step.target === 'debrief') === (place === 'debrief') && !session.pendingQuestion);
	const text = $derived(step ? step.text[i18n.locale] : null);
	const last = $derived(coach.index === FIRST_TRADE.steps.length - 1);
	let callout = $state<HTMLElement | null>(null);
	let nextButton = $state<HTMLButtonElement | null>(null);
	let pos = $state({ top: 0, left: 0 });
	let announce = $state('');
	let target: HTMLElement | null = null;

	onMount(() => {
		coach.onDone = () => session.markCoachDone(TOUR);
		if (place === 'debrief') {
			coach.debriefOpened(session.currentRunId);
			return;
		}
		const first = !!session.runCampaign && !session.progress.coachDone.includes(TOUR);
		coach.maybeStart(session.currentRunId, first);
		return () => coach.leftTerminal(session.screen === 'debrief');
	});

	// Outline and describe the target while its step is shown; move focus to the callout.
	$effect(() => {
		if (!visible || !step || !text) return;
		const el = document.querySelector<HTMLElement>(selector(step.target));
		target = el;
		announce = `${t('coach.stepOf', { n: (coach.index ?? 0) + 1, total: FIRST_TRADE.steps.length })}: ${text.title}`;
		const before = el?.getAttribute('aria-describedby') ?? null;
		el?.classList.add('coach-target');
		el?.setAttribute('aria-describedby', [before, 'coach-body'].filter(Boolean).join(' '));
		// The debrief step appears after a market key (Space, Shift+Space), often one pressed again just
		// as the run ends: focus goes to the callout, so that key never presses Next by mistake.
		const focusCallout = step.target === 'debrief';
		void tick().then(() => {
			place_();
			(focusCallout ? callout : nextButton)?.focus();
		});
		return () => {
			el?.classList.remove('coach-target');
			if (before === null) el?.removeAttribute('aria-describedby');
			else el?.setAttribute('aria-describedby', before);
		};
	});

	// A typed stop or one advance completes their steps, like Next.
	$effect(() => {
		if (!visible || !step || !target) return;
		if (step.completes === 'stop') {
			const el = target;
			el.addEventListener('input', onTyped, { once: true });
			return () => el.removeEventListener('input', onTyped);
		}
	});
	let advancedFrom: number | null = null;
	$effect(() => {
		const bar = session.state?.step ?? null;
		if (!visible || step?.completes !== 'advance') {
			advancedFrom = null;
			return;
		}
		if (advancedFrom === null) advancedFrom = bar;
		else if (bar !== advancedFrom) next();
	});

	function onTyped() {
		next();
	}

	const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

	function place_() {
		target?.scrollIntoView({ block: 'nearest' });
		const rect = target?.getBoundingClientRect();
		const h = callout?.offsetHeight ?? 160;
		const w = callout?.offsetWidth ?? 320;
		const vw = window.innerWidth;
		const vh = window.innerHeight;
		if (!rect) {
			pos = { top: Math.max(8, (vh - h) / 2), left: Math.max(8, (vw - w) / 2) };
			return;
		}
		const below = rect.bottom + 10;
		const above = rect.top - h - 10;
		const top = below + h < vh ? below : above > 8 ? above : rect.bottom - h - 8;
		// Always fully on screen, whatever the target's size and scroll position.
		pos = { top: clamp(top, 8, vh - h - 8), left: clamp(rect.left, 8, vw - w - 8) };
	}

	function end() {
		const el = target;
		if (el) (el.matches('input,button,select,textarea,[tabindex]') ? el : el.querySelector<HTMLElement>('input,button,select,[tabindex]'))?.focus();
	}

	function next() {
		coach.next();
		// The tour left this screen (ended, or waits for the debrief): focus returns to the last target.
		void tick().then(() => !visible && end());
	}

	function skip() {
		coach.skip();
		announce = t('coach.skipped');
		void tick().then(end);
	}

	function onWindowKey(e: KeyboardEvent) {
		if (!visible || e.key !== 'Escape' || document.querySelector('[aria-modal="true"]')) return;
		e.preventDefault();
		skip();
	}
</script>

<svelte:window onkeydown={onWindowKey} onresize={() => visible && place_()} />

<p class="sr-only" aria-live="polite" data-testid="coach-live">{announce}</p>
{#if visible && text}
	<div
		class="coach"
		role="dialog"
		aria-labelledby="coach-title"
		data-own-keys
		data-testid="coach-mark"
		tabindex="-1"
		data-step={step?.id}
		bind:this={callout}
		style="top: {pos.top}px; left: {pos.left}px"
	>
		<p class="muted small">{t('coach.title')} · {t('coach.stepOf', { n: (coach.index ?? 0) + 1, total: FIRST_TRADE.steps.length })}</p>
		<h2 id="coach-title">{text.title}</h2>
		<p id="coach-body">{text.body}</p>
		<div class="row">
			<button class="link" onclick={skip} data-testid="coach-skip">{t('coach.skip')}</button>
			<button class="primary" bind:this={nextButton} onclick={next} data-testid="coach-next">{last ? t('coach.done') : t('coach.next')}</button>
		</div>
	</div>
{/if}

<style>
	.coach {
		position: fixed;
		z-index: 60;
		width: min(340px, calc(100vw - 16px));
		padding: 0.6rem 0.8rem;
		background: var(--panel, var(--bg));
		border: 2px solid var(--accent);
		box-shadow: 0 0 18px rgba(57, 255, 136, 0.25);
	}
	h2 {
		font-size: 1rem;
		margin: 0.2rem 0;
	}
	p {
		margin: 0.2rem 0;
	}
	.small {
		font-size: 0.75rem;
	}
	.row {
		display: flex;
		justify-content: space-between;
		gap: 0.5rem;
		margin-top: 0.4rem;
	}
	.link {
		background: none;
		border: none;
		color: var(--muted);
		text-decoration: underline;
	}
	:global(.coach-target) {
		outline: 2px dashed var(--accent) !important;
		outline-offset: 3px;
		animation: coach-pulse 1.6s ease-in-out infinite;
	}
	@keyframes coach-pulse {
		50% {
			outline-color: transparent;
		}
	}
	@media (prefers-reduced-motion: reduce) {
		:global(.coach-target) {
			animation: none;
		}
	}
</style>
