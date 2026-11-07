<script lang="ts">
	// Academy screen (academy spec "Academy screen"; design D9): lessons by stage with their status
	// (new, done, review due), start or retake, the reviews due today and a link to the glossary.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { LESSON_INDEX } from '#lib/content/academy.ts';
	import { STAGES } from '#lib/game/curriculum.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { dueReviews, isRetired } from '#lib/game/review.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const done = $derived(session.progress.lessonsDone);
	const reviews = $derived(session.progress.lessonReviews);
	const due = $derived(dueReviews(reviews, session.today(), LESSON_INDEX.map((m) => m.id)));
	const meta = (id: string) => LESSON_INDEX.find((m) => m.id === id)!;

	function status(id: string): 'due' | 'done' | 'new' {
		if (due.includes(id)) return 'due';
		return done.includes(id) ? 'done' : 'new';
	}
</script>

<main class="academy">
	<header>
		<h1>{t('academy.title')}</h1>
		<span class="actions">
			<button onclick={() => session.openGlossary('academy')} data-testid="academy-glossary">📖 {t('glossary.open')}</button>
			<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
		</span>
	</header>
	<p class="muted">{t('academy.intro')}</p>

	{#if due.length}
		<section class="panel due" data-testid="academy-due">
			<h2 class="panel-title">{t('academy.dueTitle', { n: due.length })}</h2>
			<ul>
				{#each due as id (id)}
					<li>
						<span>{meta(id).title[L]}</span>
						<button class="primary" onclick={() => session.openLesson(id, 'academy', 'review')} data-testid="review-start">{t('academy.reviewStart')}</button>
					</li>
				{/each}
			</ul>
		</section>
	{/if}

	<ol class="stages">
		{#each STAGES as st (st.id)}
			<li class="panel" data-testid="academy-stage" data-stage={st.id}>
				<h2 class="panel-title">{t(`stage.${st.id}`)}</h2>
				<ul>
					{#each st.lessons as id (id)}
						{@const s = status(id)}
						<li class="lesson {s}" data-testid="academy-lesson" data-lesson={id} data-status={s}>
							<div>
								<strong>{meta(id).title[L]}</strong>
								<span class="tag {s === 'due' ? 'attention' : s === 'done' ? 'up' : 'info'}">{t(`academy.status.${s}`)}</span>
								{#if isRetired(reviews[id])}<span class="muted small">{t('academy.retired')}</span>{/if}
								<p class="muted small">{meta(id).summary[L]}</p>
							</div>
							<button class:primary={s === 'new'} onclick={() => session.openLesson(id, 'academy')} data-testid="academy-start">
								{s === 'new' ? t('curriculum.lessonStart') : t('curriculum.lessonRepeat')}
							</button>
						</li>
					{/each}
				</ul>
			</li>
		{/each}
	</ol>
</main>

<style>
	.academy {
		max-width: 900px;
		margin: 0 auto;
		padding: 2rem 1rem;
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 1rem;
		flex-wrap: wrap;
	}
	h1 {
		margin: 0;
	}
	.actions {
		display: flex;
		gap: 0.4rem;
	}
	.stages,
	ul {
		list-style: none;
		margin: 0;
		padding: 0;
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	.lesson,
	.due li {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 0.6rem;
	}
	.lesson p {
		margin: 0.1rem 0 0;
	}
	.small {
		font-size: 0.8rem;
	}
	.due {
		border-color: var(--attention);
	}
</style>
