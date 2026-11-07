<script lang="ts">
	// Import a run file (add-run-sharing-and-journal-stats D2, D5): the engine verifies it; an accepted
	// file is kept apart from the player's runs and can be watched at once.
	import type { GameSession, Screen } from '#lib/game/session.svelte.ts';
	import { refusalKey, RUN_FILE_EXTENSION, RUN_FILE_MAX_BYTES } from '#lib/game/runFile.ts';
	import type { ImportOutcome } from '#lib/game/runSharing.svelte.ts';
	import { i18n, t, type MessageKey } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';

	let { session, from }: { session: GameSession; from: Screen } = $props();
	let outcome = $state<ImportOutcome | null>(null);

	async function onFile(e: Event & { currentTarget: HTMLInputElement }) {
		const input = e.currentTarget;
		const file = input.files?.[0];
		input.value = '';
		if (!file) return;
		if (file.size > RUN_FILE_MAX_BYTES) return void (outcome = { ok: false, reason: 'too_large' });
		const text = await file.text().catch(() => null);
		outcome = text == null ? { ok: false, reason: 'unreadable' } : await session.sharing.importText(text);
	}

	function refusal(o: Extract<ImportOutcome, { ok: false }>): string {
		const r = refusalKey(o, session.engineStamp().executionPolicy);
		return t('runfile.refused', { reason: t(r.key as MessageKey, r.params) });
	}
</script>

<div class="import" data-testid="run-import">
	<label class="file-button">
		⇩ {t('runfile.import')}
		<input type="file" accept="{RUN_FILE_EXTENSION},.json,application/json" onchange={onFile} data-testid="run-import-file" />
	</label>
	{#if outcome?.ok}
		<p class="up" role="status" data-testid="run-import-message">
			{t('runfile.imported', { mission: missionText(outcome.record.mission, i18n.locale).title, seed: outcome.record.seed })}
			<button class="primary" onclick={() => outcome?.ok && session.watchImport(outcome.record.id, from)} data-testid="run-import-watch">▶ {t('runfile.watch')}</button>
		</p>
	{:else if outcome}
		<p class="down" role="alert" data-testid="run-import-message">{refusal(outcome)}</p>
	{/if}
</div>

<style>
	.import {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
		align-items: flex-start;
	}
	.file-button {
		cursor: pointer;
		border: 1px solid var(--line);
		border-radius: var(--radius);
		padding: 0.3rem 0.6rem;
	}
	.file-button:focus-within {
		outline: 2px solid var(--accent);
	}
	/* Visually hidden but focusable: the label is the control, the keyboard still reaches the input. */
	.file-button input {
		position: absolute;
		width: 1px;
		height: 1px;
		opacity: 0;
	}
	.import p {
		margin: 0;
		display: flex;
		flex-wrap: wrap;
		gap: 0.5rem;
		align-items: center;
	}
</style>
