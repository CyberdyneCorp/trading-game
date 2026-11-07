<script lang="ts">
	// The question of a lesson step (design D2): multiple choice, a typed number (either decimal mark)
	// or a target reached by moving the step's ticket sandbox. Feedback follows each answer; a wrong
	// one names the misconception when the step has feedback for that choice, and stays open.
	import type { Choice, LessonStep, StepText } from '#lib/content/academy.ts';
	import { sandboxOutputs, sandboxValue, type SandboxInputs } from '#lib/content/ticketSandbox.ts';
	import { fmtPct } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import type { LessonRun } from '#lib/game/lessonRun.svelte.ts';
	import RichText from './RichText.svelte';

	let { run, step, st, inputs }: { run: LessonRun; step: LessonStep; st: StepText; inputs: SandboxInputs | null } = $props();

	let typed = $state('');
	const q = $derived(step.question!);
	const choices = $derived(q.type === 'number' || q.type === 'set' ? [] : q.choices);
	const answer = $derived(q.type === 'number' || q.type === 'set' ? -1 : q.answer);
	const choiceFeedback = $derived.by(() => {
		const c = run.picked != null ? choices[run.picked] : null;
		return c?.kind === 'text' ? st.feedback?.[c.id] : undefined;
	});
	const feedback = $derived(run.status === 'right' ? st.right : (choiceFeedback ?? st.wrong));

	function choiceText(c: Choice): string {
		if (c.kind === 'bar') return t('academy.bar', { n: c.bar });
		if (c.kind === 'pct') return fmtPct(c.value / 100, i18n.locale, 2, true);
		return st.choices?.[c.id] ?? c.id;
	}

	function checkSet() {
		if (q.type !== 'set' || !inputs || step.widget?.kind !== 'ticket') return;
		run.checkSet(sandboxValue(q.field, inputs, sandboxOutputs(step.widget, inputs)));
	}

	function submitTyped(e: SubmitEvent) {
		e.preventDefault();
		run.submitNumber(typed, i18n.locale);
	}
</script>

{#if q.type === 'number'}
	<form class="typed" onsubmit={submitTyped}>
		<label>
			{t('academy.answer')}
			<input inputmode="decimal" bind:value={typed} disabled={run.solved} data-testid="lesson-number" />
			{#if q.unit}<span class="muted">{q.unit}</span>{/if}
		</label>
		<button type="submit" disabled={run.solved || !typed.trim()} data-testid="lesson-check">{t('academy.check')}</button>
	</form>
{:else if q.type === 'set'}
	<p class="typed">
		<button onclick={checkSet} disabled={run.solved} data-testid="lesson-check">{t('academy.check')}</button>
		<span class="muted small">{t('academy.setHint')}</span>
	</p>
{:else}
	<div class="choices" role="group" aria-label={st.title}>
		{#each choices as c, k (k)}
			<button
				class:right={run.picked === k && k === answer}
				class:wrong={run.picked === k && k !== answer}
				onclick={() => run.pickChoice(k)}
				disabled={run.solved && k !== answer}
				data-testid="lesson-choice"
			>
				{choiceText(c)}
			</button>
		{/each}
	</div>
{/if}
{#if run.status !== 'idle'}
	<p class={run.status === 'right' ? 'up' : 'down'} role="status" data-testid="lesson-feedback">
		<strong>{run.status === 'right' ? t('academy.right') : t('academy.wrong')}</strong>
		<RichText text={feedback ?? ''} />
	</p>
{/if}

<style>
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
	.typed {
		display: flex;
		gap: 0.5rem;
		align-items: end;
		flex-wrap: wrap;
		margin: 0.6rem 0;
	}
	.typed label {
		display: flex;
		gap: 0.4rem;
		align-items: center;
	}
	.typed input {
		width: 8rem;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
