<script lang="ts">
	import { t } from '#lib/game/i18n.svelte.ts';
	import { keymap } from '#lib/game/keymap.svelte.ts';
	import { keyLabel, SHORTCUT_ACTIONS } from '#lib/game/shortcuts.ts';
	import ShortcutSettings, { ACTION_LABEL } from './ShortcutSettings.svelte';

	let { onClose }: { onClose: () => void } = $props();
	let editing = $state(false);
</script>

<div class="modal-backdrop" role="presentation" onclick={onClose}>
	<div class="modal" role="dialog" aria-modal="true" aria-labelledby="help-title" tabindex="-1" onclick={(e) => e.stopPropagation()} onkeydown={() => {}}>
		<h2 id="help-title">{t('help.title')}</h2>
		{#if editing}
			<ShortcutSettings />
		{:else}
			<dl class="kv" data-testid="help-keys">
				{#each SHORTCUT_ACTIONS as a (a)}
					<dt><kbd>{keyLabel(keymap.map[a])}</kbd></dt>
					<dd>{t(ACTION_LABEL[a])}</dd>
				{/each}
				<dt><kbd>Esc</kbd></dt>
				<dd>{t('help.escape')}</dd>
				<dt><kbd>← → Home End</kbd></dt>
				<dd>{t('help.chartKeys')}</dd>
			</dl>
			<p class="muted">{t('help.note')}</p>
			<p class="muted">{t('brief.rules')}</p>
		{/if}
		<div class="row">
			<button onclick={() => (editing = !editing)} aria-pressed={editing} data-testid="help-remap">{t(editing ? 'keys.done' : 'keys.edit')}</button>
			<button class="primary" onclick={onClose}>OK</button>
		</div>
	</div>
</div>

<style>
	.kv {
		grid-template-columns: auto 1fr;
	}
	.kv dd {
		text-align: left;
	}
	kbd {
		border: 1px solid var(--line);
		padding: 0 0.4em;
		color: var(--accent);
	}
	.row {
		display: flex;
		gap: 0.5rem;
		justify-content: flex-end;
	}
</style>
