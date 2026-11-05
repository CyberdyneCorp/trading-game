<script lang="ts">
	// Debrief: the lesson questions with the player's answers and the key (task 4.5).
	import type { DebriefData } from '#lib/engine/types.ts';
	import { answerText, questionText } from '#lib/game/lessons.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { mission, lessons }: { mission: string; lessons: NonNullable<DebriefData['lessons']> } = $props();
	const L = $derived(i18n.locale);
	const asked = $derived(lessons.questions.some((q) => q.answered || q.skipped));
</script>

{#if asked}
	<section class="panel" data-testid="debrief-lessons">
		<h2 class="panel-title">{t('debrief.lessons')}</h2>
		<p>{t('debrief.lessonsScore', { points: lessons.points, total: lessons.total })}</p>
		{#if !lessons.reviewed}<p class="muted small">{t('debrief.lessonsUnreviewed')}</p>{/if}
		<ol>
			{#each lessons.questions as q (q.id)}
				<li data-testid="lesson-result">
					<p>{questionText(mission, q.id, L)?.prompt ?? q.id}</p>
					<p class={q.correct ? 'up' : 'down'}>
						{q.correct ? '✔' : '✖'}
						{t('lesson.yours')}: {q.answered ? answerText(mission, q, 'given', L) : t(q.skipped ? 'lesson.skipped' : 'lesson.none')}
						({t(q.correct ? 'lesson.right' : 'lesson.wrong')})
					</p>
					{#if !q.correct}<p class="muted">{t('lesson.key')}: {answerText(mission, q, 'answer', L)}</p>{/if}
				</li>
			{/each}
		</ol>
	</section>
{/if}

<style>
	.small {
		font-size: 0.8rem;
	}
	li p {
		margin: 0.15rem 0;
	}
</style>
