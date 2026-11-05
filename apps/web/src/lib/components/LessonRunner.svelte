<script lang="ts">
	// Academy lesson runner (spec curriculum "Ordered learning stages"; web-client task 4.4): concept,
	// demonstration, decisions with feedback after each answer, and a transfer exercise on another
	// market. A wrong answer explains and lets the player choose again; the first try is what counts
	// for the score. Finishing records the lesson in local progress.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { firstTryScore, lessonById, lessonText, type Choice } from '#lib/game/lessons.ts';
	import { fmtPct } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import LessonChart from './LessonChart.svelte';

	let { session }: { session: GameSession } = $props();

	const lesson = $derived(lessonById(session.lessonId));
	const text = $derived(lessonText(session.lessonId, i18n.locale));
	let i = $state(0);
	let picked = $state<number | null>(null);
	let firstTry = $state(new Map<string, boolean>());
	let finished = $state(false);

	const step = $derived(lesson?.steps[i]);
	const st = $derived(step ? text.steps[step.id] : null);
	const solved = $derived(!step?.question || picked === step.question.answer);
	const last = $derived(!!lesson && i >= lesson.steps.length - 1);
	const score = $derived(lesson ? firstTryScore(lesson, firstTry) : { right: 0, total: 0 });

	function choiceText(c: Choice): string {
		if (c.kind === 'bar') return t('lesson.bar', { n: c.bar });
		if (c.kind === 'pct') return fmtPct(c.value / 100, i18n.locale, 2, true);
		return st?.choices?.[c.id] ?? c.id;
	}

	function pick(k: number) {
		if (!step?.question || solved) return;
		picked = k;
		if (!firstTry.has(step.id)) firstTry = new Map(firstTry).set(step.id, k === step.question.answer);
	}

	function next() {
		if (!solved) return;
		if (last) finished = true;
		else {
			i += 1;
			picked = null;
		}
	}
</script>

<main class="lesson">
	{#if !lesson}
		<p class="panel down" role="alert">{t('lesson.missing')}</p>
		<button onclick={() => session.openCurriculum()}>{t('lesson.leave')}</button>
	{:else if finished}
		<section class="panel" aria-live="polite" data-testid="lesson-done">
			<h1>{t('lesson.doneTitle')}</h1>
			<p>{text.title}</p>
			<p data-testid="lesson-score">{t('lesson.doneScore', { right: score.right, total: score.total })}</p>
			<p class="muted">{t('lesson.doneNote')}</p>
			<button class="primary" onclick={() => session.finishLesson(lesson.id)} data-testid="lesson-finish">{t('lesson.finish')}</button>
		</section>
	{:else if step && st}
		<section class="panel" aria-labelledby="lesson-step-title">
			<p class="muted head">
				<span>{text.title} · <span class="tag info" data-testid="lesson-phase">{t(`lesson.phase.${step.phase}`)}</span></span>
				<span data-testid="lesson-step">{t('tut.step', { n: i + 1, total: lesson.steps.length })}</span>
			</p>
			<h1 id="lesson-step-title">{st.title}</h1>
			{#if step.chart}<LessonChart chart={step.chart} />{/if}
			<p class="body">{st.body}</p>
			{#if step.question}
				<div class="choices" role="group" aria-label={st.title}>
					{#each step.question.choices as c, k (k)}
						<button
							class:right={picked === k && k === step.question.answer}
							class:wrong={picked === k && k !== step.question.answer}
							onclick={() => pick(k)}
							disabled={solved && k !== step.question.answer}
							data-testid="lesson-choice"
						>
							{choiceText(c)}
						</button>
					{/each}
				</div>
				{#if picked !== null}
					<p class={solved ? 'up' : 'down'} role="status" data-testid="lesson-feedback">
						<strong>{solved ? t('lesson.right') : t('lesson.wrong')}</strong>
						{solved ? st.right : st.wrong}
					</p>
				{/if}
			{/if}
			<div class="actions">
				<button class="link" onclick={() => session.openCurriculum()}>{t('lesson.leave')}</button>
				<span class="nav">
					<button onclick={() => ((i -= 1), (picked = null))} disabled={i === 0}>{t('tut.prev')}</button>
					<button class="primary" onclick={next} disabled={!solved} data-testid="lesson-next">
						{last ? t('lesson.complete') : t('tut.next')}
					</button>
				</span>
			</div>
		</section>
	{/if}
</main>

<style>
	.lesson {
		max-width: 680px;
		margin: 2rem auto;
		padding: 0 1rem;
	}
	.head {
		display: flex;
		justify-content: space-between;
		font-size: 0.8rem;
		margin: 0 0 0.6rem;
	}
	h1 {
		font-size: 1.3rem;
		margin: 0.6rem 0 0.3rem;
	}
	.body {
		line-height: 1.5;
	}
	.choices {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
		gap: 0.4rem;
		margin: 0.6rem 0;
	}
	.choices button.right {
		border-color: var(--up);
		color: var(--up);
	}
	.choices button.wrong {
		border-color: var(--down);
		color: var(--down);
	}
	.actions {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 0.6rem;
		flex-wrap: wrap;
		margin-top: 0.8rem;
	}
	.nav {
		display: flex;
		gap: 0.4rem;
	}
	.link {
		background: none;
		border: none;
		color: var(--muted);
		text-decoration: underline;
	}
</style>
