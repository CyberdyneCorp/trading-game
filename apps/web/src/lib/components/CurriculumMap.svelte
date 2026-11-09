<script lang="ts">
	// Curriculum map and progress (spec curriculum; web-client task 4.4): every stage in dependency
	// order with its status, assessment missions, Academy lessons (x/y done, start or retake) and
	// unlocks, plus the player's progress. Lessons never gate a stage that has missions (design D9).
	// Branch stages (add-practice-and-advanced-strategies D14) are drawn beside the path, under the stage
	// they grow from; an elective one says so.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { bestCompletedCapital } from '#lib/game/campaign.ts';
	import { curriculumSummary, lessonProgress, LOCKED_LEVERAGE_CAP, STAGES, stageStatus, type Unlock } from '#lib/game/curriculum.ts';
	import { fmtMoney } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { lessonMeta } from '#lib/content/academy.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const assessed = $derived(session.progress.assessed);
	const lessonsDone = $derived(session.progress.lessonsDone);
	const summary = $derived(curriculumSummary(assessed, lessonsDone));
	const best = $derived(bestCompletedCapital(session.progress.campaignHistory));
	const unlockText = (u: Unlock) => t(`unlock.${u}`, { cap: LOCKED_LEVERAGE_CAP });
	const missionsText = (ids: string[]) =>
		ids.map((m) => `${assessed.includes(m) ? '✓ ' : ''}${missionText(m, L).title}`).join(', ');
</script>

<main class="curriculum">
	<header>
		<h1>{t('curriculum.title')}</h1>
		<span class="actions">
			<button onclick={() => session.openAcademy()} data-testid="curriculum-academy">🎓 {t('academy.open')}</button>
			<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
		</span>
	</header>
	<p class="muted">{t('curriculum.note')}</p>

	<section class="panel progress" data-testid="progress">
		<h2 class="panel-title">{t('curriculum.progress')}</h2>
		<p data-testid="curriculum-summary">{t('curriculum.summary', { passed: summary.passed, total: summary.withContent })}</p>
		<p>{t('curriculum.completed', { n: session.completed.length, total: session.missions.length })}</p>
		<p>
			{t('campaign.played', { n: session.progress.campaignHistory.length })}{#if best}{' · '}{t('campaign.best', { capital: fmtMoney(best, L) })}{/if}
		</p>
	</section>

	<ol class="stages">
		{#each STAGES as st, i (st.id)}
			{@const status = stageStatus(st.id, assessed, lessonsDone)}
			<li class="panel stage {status}" class:branch={st.branch} data-testid="stage" data-stage={st.id} data-status={status} data-branch={st.branch ? 'true' : undefined}>
				<div class="head">
					<span class="idx" aria-hidden={st.branch ? 'true' : undefined}>{st.branch ? '↳' : i + 1}</span>
					<strong>{t(`stage.${st.id}`)}</strong>
					<span class="tag status">{t(`curriculum.status.${status}`)}</span>
					{#if st.campaign === false}<span class="tag info" data-testid="stage-elective">{t('curriculum.elective')}</span>{/if}
				</div>
				{#if st.branch}
					<p class="muted small" data-testid="stage-branch">{t('curriculum.branch', { stage: t(`stage.${st.requires[0]}`) })}</p>
				{/if}
				{#if st.campaign === false}<p class="muted small">{t('curriculum.electiveNote')}</p>{/if}
				{#if st.missions.length}
					<p>{t('curriculum.assessment', { missions: missionsText(st.missions) })}</p>
				{:else if st.lessons.length}
					<p class="muted small">{t('curriculum.lessonNote')}</p>
				{:else}
					<p class="muted small">{t('curriculum.soonNote')}</p>
				{/if}
				{#if st.lessons.length}
					{@const lp = lessonProgress(st.id, lessonsDone)}
					<p class="muted small" data-testid="stage-lessons">{t('curriculum.lessons', { done: lp.done, total: lp.total })}</p>
				{/if}
				{#each st.lessons as id (id)}
					{@const done = lessonsDone.includes(id)}
					<p class="lesson-row" data-testid="stage-lesson" data-lesson={id}>
						<span>{t('curriculum.lesson', { title: lessonMeta(id)?.title[L] ?? id })}{#if done}{' '}<span class="up">✓ {t('curriculum.lessonDone')}</span>{/if}</span>
						<button class:primary={!done} onclick={() => session.openLesson(id)} data-testid="lesson-start">
							{done ? t('curriculum.lessonRepeat') : t('curriculum.lessonStart')}
						</button>
					</p>
				{/each}
				{#if st.unlocks.length}
					<p class="info">{t('curriculum.unlocks', { what: st.unlocks.map(unlockText).join(', ') })}</p>
				{/if}
			</li>
		{/each}
	</ol>
</main>

<style>
	.curriculum {
		max-width: 900px;
		margin: 0 auto;
		padding: 2rem 1rem;
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}
	.actions {
		display: flex;
		gap: 0.4rem;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 1rem;
	}
	h1 {
		margin: 0;
	}
	.progress p,
	.stage p {
		margin: 0.2rem 0 0;
	}
	.stages {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	.head {
		display: flex;
		align-items: baseline;
		gap: 0.6rem;
	}
	.stage.branch {
		margin-left: 2.2rem;
		border-left: 3px solid var(--accent, currentColor);
	}
	.idx {
		font-family: var(--font-pixel);
		color: var(--muted);
		min-width: 1.6rem;
	}
	.status {
		margin-left: auto;
	}
	.stage.passed {
		border-color: var(--up);
	}
	.stage.passed .status {
		color: var(--up);
	}
	.stage.open {
		border-color: var(--accent);
	}
	.stage.locked .status {
		color: var(--attention);
	}
	.stage.soon {
		opacity: 0.75;
	}
	.small {
		font-size: 0.8rem;
	}
	.lesson-row {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 0.6rem;
	}
</style>
