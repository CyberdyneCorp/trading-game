<script lang="ts">
	// The glossary opened at a term from an in-place definition ("more"), over any screen.
	import { glossaryUi } from '#lib/game/glossaryUi.svelte.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import GlossaryList from './GlossaryList.svelte';

	let { seen = [] }: { seen?: readonly string[] } = $props();
	let returnTo: HTMLElement | null = null;
	let dialog = $state<HTMLElement | null>(null);

	$effect(() => {
		returnTo = document.activeElement as HTMLElement | null;
		dialog?.focus();
		return () => returnTo?.focus?.();
	});

	function onKeydown(e: KeyboardEvent) {
		if (e.key !== 'Escape') return;
		e.stopPropagation();
		glossaryUi.close();
	}
</script>

<div class="modal-backdrop" role="presentation" onclick={() => glossaryUi.close()}>
	<div
		class="modal glossary-dialog"
		role="dialog"
		aria-modal="true"
		aria-labelledby="glossary-dialog-title"
		tabindex="-1"
		bind:this={dialog}
		onclick={(e) => e.stopPropagation()}
		onkeydown={onKeydown}
		data-testid="glossary-dialog"
	>
		<h2 id="glossary-dialog-title">{t('glossary.title')}</h2>
		<GlossaryList focus={glossaryUi.focus} {seen} />
		<div class="row">
			<button class="primary" onclick={() => glossaryUi.close()}>{t('glossary.close')}</button>
		</div>
	</div>
</div>

<style>
	.glossary-dialog {
		width: min(860px, 94vw);
		max-height: 90vh;
		overflow-y: auto;
	}
	.row {
		display: flex;
		justify-content: flex-end;
	}
</style>
