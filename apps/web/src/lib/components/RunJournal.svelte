<script lang="ts">
	// In-game journal (shortcut J): the plans recorded in this run so far. Modal like the help overlay.
	import type { PlanEntry } from '#lib/game/analysis.ts';
	import { t } from '#lib/game/i18n.svelte.ts';
	import JournalEntries from './JournalEntries.svelte';

	let { entries, onClose }: { entries: readonly PlanEntry[]; onClose: () => void } = $props();
</script>

<div class="modal-backdrop" role="presentation" onclick={onClose}>
	<div class="modal" role="dialog" aria-modal="true" aria-labelledby="run-journal-title" tabindex="-1" onclick={(e) => e.stopPropagation()} onkeydown={() => {}}>
		<h2 id="run-journal-title">{t('runJournal.title')}</h2>
		{#if entries.length === 0}
			<p class="muted" data-testid="run-journal-empty">{t('runJournal.empty')}</p>
		{:else}
			<JournalEntries {entries} />
		{/if}
		<button class="primary" onclick={onClose}>OK</button>
	</div>
</div>
