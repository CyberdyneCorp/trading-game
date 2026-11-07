<script lang="ts">
	// The menu's spaced-review prompt (academy spec "Completion, retakes and spaced review"): how many
	// lessons are due, a way to the Academy, and a dismissal that lasts until the next calendar day.
	// It never blocks play. The dismissal is a per-device convenience kept in localStorage.
	import { LESSON_INDEX } from '#lib/content/academy.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { tCount, t } from '#lib/game/i18n.svelte.ts';
	import { dueReviews } from '#lib/game/review.ts';

	let { session }: { session: GameSession } = $props();

	const KEY = 'tradelab.reviewDismissed.v1';
	const today = $derived(session.today());
	let dismissedOn = $state(read());
	const due = $derived(dueReviews(session.progress.lessonReviews, today, LESSON_INDEX.map((m) => m.id)));

	function read(): string | null {
		try {
			return localStorage.getItem(KEY);
		} catch {
			return null;
		}
	}

	function dismiss() {
		dismissedOn = today;
		try {
			localStorage.setItem(KEY, today);
		} catch {
			// private mode: the dismissal lasts until the page reloads
		}
	}
</script>

{#if due.length && dismissedOn !== today}
	<section class="panel banner review" role="status" data-testid="review-prompt">
		<span>📚 {tCount('academy.reviewPrompt', due.length)}</span>
		<span class="actions">
			<button class="primary" onclick={() => session.openAcademy()} data-testid="review-open">{t('academy.reviewOpen')}</button>
			<button onclick={dismiss} data-testid="review-dismiss">{t('academy.reviewDismiss')}</button>
		</span>
	</section>
{/if}

<style>
	.review {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 0.6rem;
		flex-wrap: wrap;
		border-color: var(--info);
	}
	.actions {
		display: flex;
		gap: 0.4rem;
	}
</style>
