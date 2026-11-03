<script lang="ts">
	import { t } from '#lib/game/i18n.svelte.ts';
	let { onClose }: { onClose: () => void } = $props();
	const rows: [string, Parameters<typeof t>[0]][] = [
		['Space', 'help.advance'],
		['Shift + Space', 'help.advance5'],
		['B', 'help.buy'],
		['S', 'help.sell'],
		['X', 'help.close'],
		['?', 'help.help'],
		['Esc', 'help.escape']
	];
</script>

<div class="modal-backdrop" role="presentation" onclick={onClose}>
	<div class="modal" role="dialog" aria-modal="true" aria-labelledby="help-title" tabindex="-1" onclick={(e) => e.stopPropagation()} onkeydown={() => {}}>
		<h2 id="help-title">{t('help.title')}</h2>
		<dl class="kv">
			{#each rows as [key, label] (key)}
				<dt><kbd>{key}</kbd></dt>
				<dd>{t(label)}</dd>
			{/each}
		</dl>
		<p class="muted">{t('help.note')}</p>
		<p class="muted">{t('brief.rules')}</p>
		<button class="primary" onclick={onClose}>OK</button>
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
</style>
