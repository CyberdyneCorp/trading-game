<script lang="ts">
	// Academy lesson runner (design D2; spec academy): concept, demonstration, decisions with feedback
	// after each answer, and a transfer exercise on another market. Each step can show a chart, a
	// ticket sandbox, a payoff diagram or a table, and ask a choice, a typed number or a target to reach
	// with the sandbox. A wrong answer explains and lets the player answer again; the first try is what
	// counts. A spaced review runs only the decision and transfer questions. The lesson file loads
	// lazily, so lessons stay out of the initial bundle.
	import { untrack } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { lessonText, loadLesson } from '#lib/content/academy.ts';
	import { initialInputs, type SandboxInputs } from '#lib/content/ticketSandbox.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { LessonRun } from '#lib/game/lessonRun.svelte.ts';
	import LessonChart from './LessonChart.svelte';
	import LessonPayoff from './LessonPayoff.svelte';
	import LessonQuestion from './LessonQuestion.svelte';
	import LessonTable from './LessonTable.svelte';
	import LessonTicket from './LessonTicket.svelte';
	import RichText from './RichText.svelte';

	let { session }: { session: GameSession } = $props();

	let run = $state<LessonRun | null>(null);
	let missing = $state(false);
	let inputs = $state<SandboxInputs | null>(null);

	$effect(() => {
		const id = session.lessonId;
		const mode = session.lessonMode;
		untrack(() => {
			run = null;
			missing = false;
		});
		void loadLesson(id).then((l) => {
			if (session.lessonId !== id) return;
			if (l) run = new LessonRun(l, mode);
			else missing = true;
		});
	});

	const text = $derived(run ? lessonText(run.lesson, i18n.locale) : null);
	const step = $derived(run?.step);
	const st = $derived(step && text ? text.steps[step.id] : null);
	const review = $derived(session.lessonMode === 'review');

	// A fresh sandbox for every step that shows one.
	$effect(() => {
		const w = step?.widget;
		untrack(() => (inputs = w?.kind === 'ticket' ? initialInputs(w) : null));
	});

	const finish = () => run && session.finishLesson(run.lesson.id, run.allRightFirstTry);
</script>

<main class="lesson">
	{#if missing}
		<p class="panel down" role="alert">{t('academy.missing')}</p>
		<button onclick={() => session.leaveLesson()}>{t('academy.leave')}</button>
	{:else if !run || !text}
		<p class="muted" role="status">{t('state.loading')}</p>
	{:else if run.finished}
		<section class="panel" aria-live="polite" data-testid="lesson-done">
			<h1>{review ? t('academy.reviewDoneTitle') : t('academy.doneTitle')}</h1>
			<p>{text.title}</p>
			<p data-testid="lesson-score">{t('academy.doneScore', { right: run.score.right, total: run.score.total })}</p>
			{#if review}
				<p data-testid="review-outcome">{run.allRightFirstTry ? t('academy.reviewUp') : t('academy.reviewReset')}</p>
			{:else}
				<p class="muted">{t('academy.reviewNext')}</p>
			{/if}
			<p class="muted">{t('academy.doneNote')}</p>
			<button class="primary" onclick={finish} data-testid="lesson-finish">{t(session.lessonReturn === 'academy' ? 'academy.backToAcademy' : 'academy.finish')}</button>
		</section>
	{:else if step && st}
		<section class="panel" aria-labelledby="lesson-step-title">
			<p class="muted head">
				<span>{text.title}{review ? ` · ${t('academy.review')}` : ''} · <span class="tag info" data-testid="lesson-phase">{t(`academy.phase.${step.phase}`)}</span></span>
				<span data-testid="lesson-step">{t('tut.step', { n: run.i + 1, total: run.steps.length })}</span>
			</p>
			<h1 id="lesson-step-title">{st.title}</h1>
			{#if step.chart}<LessonChart chart={step.chart} />{/if}
			{#if step.widget?.kind === 'ticket' && inputs}
				<LessonTicket widget={step.widget} bind:inputs />
			{:else if step.widget?.kind === 'payoff'}
				<LessonPayoff widget={step.widget} />
			{:else if step.widget?.kind === 'table'}
				<LessonTable widget={step.widget} text={st} />
			{/if}
			<p class="body"><RichText text={st.body} /></p>
			{#if step.question}
				{#key step.id}<LessonQuestion {run} {step} {st} {inputs} />{/key}
			{/if}
			<div class="actions">
				<button class="link" onclick={() => session.leaveLesson()}>{t('academy.leave')}</button>
				<span class="nav">
					<button onclick={() => run?.prev()} disabled={run.i === 0}>{t('tut.prev')}</button>
					<button class="primary" onclick={() => run?.next()} disabled={!run.solved} data-testid="lesson-next">
						{run.last ? t('academy.complete') : t('tut.next')}
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
