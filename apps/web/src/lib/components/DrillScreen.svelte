<script lang="ts">
	// Daily drills (add-practice-and-advanced-strategies design D15): six items fed by the spaced review,
	// a soft two-minute clock (shown, never failing; off in the settings), the worked explanation after a
	// wrong answer, and an end screen with the first-try results and each item's lesson.
	import { onDestroy } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t, type MessageKey } from '#lib/game/i18n.svelte.ts';
	import { settings } from '#lib/game/settings.svelte.ts';
	import { DRILL_SECONDS, buildSession, isRight, itemText, parseAnswer, sessionIndex, type DrillItem, type ItemResult } from '#lib/game/drills.ts';
	import { DRILL_TEMPLATES } from '#lib/content/drills.ts';
	import { lessonMeta } from '#lib/content/academy.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);

	let items = $state<DrillItem[]>([]);
	let results = $state<(ItemResult | null)[]>([]);
	let index = $state(0);
	let typed = $state('');
	let feedback = $state<{ right: boolean; invalid?: boolean } | null>(null);
	let phase = $state<'intro' | 'asking' | 'done'>('intro');
	let left = $state(DRILL_SECONDS);
	let timeUp = $state(false);
	let timer: ReturnType<typeof setInterval> | null = null;

	const template = (it: DrillItem) => DRILL_TEMPLATES.find((x) => x.id === it.template)!;
	const item = $derived(items[index]);
	const text = $derived(item ? itemText(item, template(item), L) : null);
	const firstTry = $derived(results.filter((r) => r === 'right').length);
	const clock = $derived(`${Math.floor(left / 60)}:${String(left % 60).padStart(2, '0')}`);

	function start(): void {
		const p = session.progress;
		const date = session.today();
		items = buildSession({
			date,
			now: Date.now(),
			session: sessionIndex(p.drillHistory, date),
			lessonReviews: p.lessonReviews,
			drillReviews: p.drillReviews,
			history: p.drillHistory
		});
		results = items.map(() => null);
		index = 0;
		typed = '';
		feedback = null;
		timeUp = false;
		left = DRILL_SECONDS;
		phase = 'asking';
		stopClock();
		if (settings.drillTimer) timer = setInterval(tick, 1000);
	}

	function tick(): void {
		left = Math.max(0, left - 1);
		if (left === 0) {
			timeUp = true;
			finish();
		}
	}

	function stopClock(): void {
		if (timer) clearInterval(timer);
		timer = null;
	}

	function answer(given: number): void {
		if (!item || feedback?.right === false || feedback?.right) return;
		if (!item.choice && !Number.isFinite(given)) {
			feedback = { right: false, invalid: true };
			return;
		}
		const right = isRight(item, given);
		results[index] = right ? 'right' : 'wrong';
		feedback = { right };
	}

	function next(): void {
		if (index + 1 >= items.length) return finish();
		index += 1;
		typed = '';
		feedback = null;
	}

	function finish(): void {
		stopClock();
		if (phase !== 'asking') return;
		const final = results.map((r) => r ?? 'skipped');
		results = final;
		session.finishDrills(items, final);
		phase = 'done';
	}

	onDestroy(stopClock);
	const resultKey = (r: ItemResult | null): MessageKey => `drills.result.${r ?? 'skipped'}` as MessageKey;
	const answered = $derived(feedback != null && !feedback.invalid);
</script>

<main class="drills" data-testid="drills">
	<header class="row">
		<h1 class="glow">{t('drills.title')}</h1>
		<button onclick={() => session.backToMenu()} data-testid="drills-back">{t('practice.back')}</button>
	</header>

	{#if phase === 'intro'}
		<section class="panel">
			<p>{t('drills.intro')}</p>
			<label class="check">
				<input type="checkbox" checked={settings.drillTimer} onchange={(e) => ((settings.drillTimer = e.currentTarget.checked), settings.save())} data-testid="drills-timer" />
				{t('drills.timerSetting')}
			</label>
			<p class="muted small">{t('drills.reviewNote')}</p>
			<button class="primary" onclick={start} data-testid="drills-start">{t('drills.start')}</button>
		</section>
	{:else if phase === 'asking' && item && text}
		<section class="panel" aria-live="polite">
			<p class="row small muted">
				<span data-testid="drills-position">{t('drills.item', { n: index + 1, total: items.length })} · {t(`drills.skill.${item.skill}` as MessageKey)}</span>
				<span role="timer" data-testid="drills-clock">{settings.drillTimer ? t('drills.timeLeft', { time: clock }) : t('drills.noClock')}</span>
			</p>
			<p class="prompt" data-testid="drills-prompt">{text.prompt}</p>
			{#if item.choice}
				<div class="choices" role="group" aria-label={t('drills.answer')}>
					{#each text.choices as c, k (k)}
						<button onclick={() => answer(k)} disabled={answered} data-testid="drills-choice">{c}</button>
					{/each}
				</div>
			{:else}
				<form
					class="row"
					onsubmit={(e) => {
						e.preventDefault();
						answer(parseAnswer(typed));
					}}
				>
					<label>
						{t('drills.answer')}
						<input inputmode="decimal" bind:value={typed} disabled={answered} data-testid="drills-input" />
					</label>
					<button class="primary" type="submit" disabled={answered} data-testid="drills-check">{t('drills.check')}</button>
				</form>
			{/if}
			{#if feedback?.invalid}
				<p class="attention" role="status">{t('drills.invalid')}</p>
			{:else if feedback}
				<p class={feedback.right ? 'up' : 'down'} role="status" data-testid="drills-feedback">
					{feedback.right ? t('drills.right') : t('drills.wrong', { answer: text.answer })}
				</p>
				{#if !feedback.right}<p class="explain" data-testid="drills-explain">{text.explain}</p>{/if}
				<button class="primary" onclick={next} data-testid="drills-next">{index + 1 >= items.length ? t('drills.finish') : t('drills.next')}</button>
			{/if}
		</section>
	{:else if phase === 'done'}
		<section class="panel" data-testid="drills-end">
			<h2 class="panel-title">{t('drills.endTitle')}</h2>
			{#if timeUp}<p class="attention">{t('drills.timeUp')}</p>{/if}
			<p data-testid="drills-first-try">{t('drills.firstTry', { n: firstTry, total: items.length })}</p>
			<ol>
				{#each items as it, k (k)}
					{@const meta = lessonMeta(it.lessons[0])}
					<li>
						<span class="tag {results[k] === 'right' ? 'up' : results[k] === 'wrong' ? 'down' : 'muted'}">{t(resultKey(results[k]))}</span>
						{itemText(it, template(it), L).prompt}
						{#if meta}
							<button class="link" onclick={() => session.openLesson(it.lessons[0], 'drills')} data-testid="drills-lesson">🎓 {t('drills.lesson', { title: meta.title[L] })}</button>
						{/if}
					</li>
				{/each}
			</ol>
			<button class="primary" onclick={start} data-testid="drills-again">{t('drills.again')}</button>
		</section>
	{/if}
</main>

<style>
	.drills {
		max-width: 48rem;
		margin: 0 auto;
		padding: 1rem;
	}
	.row {
		display: flex;
		gap: 1rem;
		justify-content: space-between;
		align-items: center;
		flex-wrap: wrap;
	}
	.prompt {
		font-size: 1.15rem;
		line-height: 1.5;
	}
	.choices {
		display: flex;
		gap: 0.5rem;
		flex-wrap: wrap;
	}
	.explain {
		border-left: 3px solid var(--accent);
		padding-left: 0.75rem;
	}
	.check {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}
	ol li {
		margin-bottom: 0.5rem;
	}
</style>
