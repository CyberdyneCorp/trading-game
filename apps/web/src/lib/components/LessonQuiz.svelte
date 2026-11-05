<script lang="ts">
	// Lesson questions on the briefing (task 4.5): each answer is an engine command, recorded once and
	// corrected in the debrief. Numeric answers accept a decimal comma or point.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { numericAnswer, showableQuestions } from '#lib/game/lessons.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let { session }: { session: GameSession } = $props();
	const info = $derived(session.lessonInfo);
	const questions = $derived(info ? showableQuestions(info, i18n.locale) : []);
	let choice = $state<Record<number, number>>({});
	let typed = $state<Record<number, string>>({});
	let invalid = $state<Record<number, boolean>>({});

	function isNumeric(index: number): boolean {
		return info?.questions[index]?.kind === 'numeric';
	}

	async function answer(index: number) {
		if (!isNumeric(index)) {
			if (choice[index] == null) return;
			await session.answerLesson(index, { choice: choice[index] });
			return;
		}
		const value = numericAnswer(typed[index] ?? '');
		invalid = { ...invalid, [index]: value == null };
		if (value != null) await session.answerLesson(index, { value });
	}
</script>

{#if info && questions.length}
	<section class="quiz" data-testid="lesson-quiz">
		<h3>{t('lesson.title')}</h3>
		<p class="muted small">{t('lesson.intro')}</p>
		{#if !info.reviewed}<p class="muted small" data-testid="lesson-unreviewed">{t('lesson.unreviewed')}</p>{/if}
		{#each questions as q (q.id)}
			{@const done = session.lessonsAnswered.includes(q.index)}
			<fieldset disabled={done || session.busy} data-testid="lesson-question">
				<legend>{q.text.prompt}</legend>
				{#if q.text.choices.length}
					{#each q.text.choices as label, i (i)}
						<label class="choice">
							<input type="radio" name={`lesson-${q.id}`} value={i} bind:group={choice[q.index]} />
							{label}
						</label>
					{/each}
				{:else}
					<label class="numeric">
						<input type="text" inputmode="decimal" bind:value={typed[q.index]} aria-invalid={invalid[q.index] ?? false} />
						{q.text.unit ?? ''}
					</label>
					{#if invalid[q.index]}<p class="small attention">{t('lesson.invalid')}</p>{/if}
				{/if}
				{#if done}
					<p class="small muted" data-testid="lesson-done">✓ {t('lesson.answered')}</p>
				{:else}
					<div class="row">
						<button onclick={() => answer(q.index)}>{t('lesson.answer')}</button>
						<button class="ghost" onclick={() => session.answerLesson(q.index, null)}>{t('lesson.skip')}</button>
					</div>
				{/if}
			</fieldset>
		{/each}
	</section>
{/if}

<style>
	.quiz {
		margin: 1rem 0;
	}
	fieldset {
		border: 1px solid var(--border, #444);
		margin: 0.5rem 0;
		padding: 0.5rem 0.8rem;
	}
	legend {
		padding: 0 0.3rem;
	}
	.choice {
		display: block;
		margin: 0.2rem 0;
	}
	.numeric input {
		width: 8rem;
	}
	.row {
		display: flex;
		gap: 0.4rem;
		margin-top: 0.4rem;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
