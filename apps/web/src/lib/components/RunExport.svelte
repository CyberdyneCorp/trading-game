<script lang="ts">
	// Export a run as a run file (add-run-sharing-and-journal-stats D1–D3): the run on screen, or the
	// player's run `runId`. Plan notes go only when ticked.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import type { ExportOutcome } from '#lib/game/runSharing.svelte.ts';
	import { refusalKey } from '#lib/game/runFile.ts';
	import { t, type MessageKey } from '#lib/game/i18n.svelte.ts';

	let { session, runId = null, compact = false }: { session: GameSession; runId?: string | null; compact?: boolean } = $props();
	let notes = $state(false);
	let message = $state<{ ok: boolean; text: string } | null>(null);
	const isImport = $derived(!runId && session.viewing?.kind === 'import');

	function download(name: string, text: string) {
		const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
		const a = document.createElement('a');
		a.href = url;
		a.download = name;
		a.click();
		URL.revokeObjectURL(url);
	}

	function explain(out: Extract<ExportOutcome, { ok: false }>): string {
		if (out.reason === 'private_pack' || out.reason === 'pack_missing' || out.reason === 'no_actions') return t(`runfile.block.${out.reason}` as MessageKey);
		const r = refusalKey({ reason: out.reason, detail: out.detail }, session.engineStamp().executionPolicy);
		return t('runfile.refused', { reason: t(r.key as MessageKey, r.params) });
	}

	async function exportRun() {
		const out = runId ? await session.sharing.exportRun(runId, notes) : await session.exportShown(notes);
		if (!out.ok) return void (message = { ok: false, text: explain(out) });
		download(out.name, out.text);
		message = { ok: !out.mismatch, text: out.mismatch ? t('runfile.mismatch') : t('runfile.exported', { name: out.name }) };
	}
</script>

<div class="export" class:compact data-testid="run-export">
	<button onclick={exportRun} disabled={session.busy} data-testid="run-export-button">⇪ {t('runfile.export')}</button>
	{#if !isImport}
		<label><input type="checkbox" bind:checked={notes} data-testid="run-export-notes" /> {t('runfile.includeNotes')}</label>
		{#if !compact}<p class="muted small">{t('runfile.notesHint')}</p>{/if}
	{/if}
	{#if message}<p class={message.ok ? 'up' : 'down'} role="status" data-testid="run-export-message">{message.text}</p>{/if}
</div>

<style>
	.export {
		display: flex;
		flex-wrap: wrap;
		gap: 0.5rem;
		align-items: center;
	}
	.export p {
		flex-basis: 100%;
		margin: 0;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
