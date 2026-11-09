<script lang="ts">
	// A combo whose limit legs still work after an open (design D2 of add-practice-and-advanced-strategies):
	// how many legs filled and a way to cancel the rest. Cancelling filled-and-working legs leaves an
	// incomplete combo, which the incomplete notice then names. Nothing is cancelled without the click.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { workingLegs, type ComboStatus } from '#lib/game/combos.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import Term from './Term.svelte';

	let { session, status }: { session: GameSession; status: ComboStatus } = $props();

	const done = $derived(status.legs.filter((l) => l.state === 'filled').length);
	const ids = $derived(workingLegs(status));
	const tag = $derived({ combo: status.record.id, strategy: status.record.strategy });

	async function cancelWorking() {
		await session.sequence(async () => {
			for (const id of ids) await session.cancel(id, tag);
		});
	}
</script>

<section class="panel combo-working" role="status" aria-labelledby="combo-working-title" data-testid="combo-working">
	<h2 id="combo-working-title" class="panel-title">⏳ {t('combo.workingTitle', { name: t(`strategy.${status.record.strategy}`) })}</h2>
	<p class="small">{t('combo.working', { done, total: status.legs.length })} <Term id="legging-risk">{t('combo.leggingRisk')}</Term></p>
	<button onclick={cancelWorking} disabled={session.busy || ids.length === 0} data-testid="combo-cancel-working">{t('combo.cancelWorking')}</button>
</section>

<style>
	.combo-working {
		border-color: var(--attention);
	}
	.small {
		font-size: 0.8rem;
		margin: 0 0 0.3rem;
	}
</style>
