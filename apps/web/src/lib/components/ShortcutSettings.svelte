<script lang="ts" module>
	import type { MessageKey } from '#lib/game/i18n.svelte.ts';
	import type { ShortcutAction } from '#lib/game/shortcuts.ts';

	/** Label of each remappable action (help overlay and settings). */
	export const ACTION_LABEL: Record<ShortcutAction, MessageKey> = {
		advance: 'help.advance',
		advance5: 'help.advance5',
		buy: 'help.buy',
		sell: 'help.sell',
		close: 'help.close',
		patterns: 'help.patterns',
		auto: 'help.auto',
		mute: 'help.sound',
		options: 'help.options',
		journal: 'help.journal',
		help: 'help.help'
	};
</script>

<script lang="ts">
	// Shortcut remapping (spec web-client "Keyboard operation"): pick an action, press its new key.
	// A key another action uses, or a reserved one (Enter, Esc, Tab, arrows), is refused with the reason.
	import { t } from '#lib/game/i18n.svelte.ts';
	import { keymap } from '#lib/game/keymap.svelte.ts';
	import { keyLabel, keyName, SHORTCUT_ACTIONS } from '#lib/game/shortcuts.ts';

	let waiting = $state<ShortcutAction | null>(null);
	let message = $state<{ ok: boolean; text: string } | null>(null);

	function start(a: ShortcutAction) {
		waiting = a;
		keymap.capturing = true;
		message = null;
	}

	function stop() {
		waiting = null;
		keymap.capturing = false;
	}

	function resultText(a: ShortcutAction, key: string): { ok: boolean; text: string } {
		const res = keymap.set(a, key);
		const label = keyLabel(key);
		if (res.ok) return { ok: true, text: t('keys.saved', { action: t(ACTION_LABEL[a]), key: label }) };
		if (res.error === 'reserved') return { ok: false, text: t('keys.reserved', { key: label }) };
		return { ok: false, text: t('keys.conflict', { key: label, action: t(ACTION_LABEL[res.other]) }) };
	}

	// Capture phase on the window, so the game's own shortcuts never see the key being assigned.
	function onKey(e: KeyboardEvent) {
		if (!waiting) return;
		e.preventDefault();
		e.stopPropagation();
		if (e.key === 'Escape') return stop();
		const key = keyName(e);
		if (!key || e.ctrlKey || e.metaKey || e.altKey) return;
		message = resultText(waiting, key);
		stop();
	}

	$effect(() => () => {
		keymap.capturing = false;
	});
</script>

<svelte:window onkeydowncapture={onKey} />

<div class="keys" data-testid="shortcut-settings">
	<table>
		<tbody>
			{#each SHORTCUT_ACTIONS as a (a)}
				<tr data-action={a}>
					<td>{t(ACTION_LABEL[a])}</td>
					<td><kbd data-testid="key-{a}">{keyLabel(keymap.map[a])}</kbd></td>
					<td>
						<button onclick={() => start(a)} aria-pressed={waiting === a} data-testid="rebind-{a}">
							{waiting === a ? t('keys.press') : t('keys.change')}
						</button>
					</td>
				</tr>
			{/each}
		</tbody>
	</table>
	{#if message}<p class={message.ok ? 'up' : 'down'} role="status" data-testid="keys-message">{message.text}</p>{/if}
	<p class="muted small">{t('help.note')}</p>
	<button onclick={() => (keymap.reset(), (message = null))} data-testid="keys-reset">{t('keys.reset')}</button>
</div>

<style>
	.keys {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
	}
	table {
		border-collapse: collapse;
	}
	td {
		padding: 0.1rem 0.5rem 0.1rem 0;
	}
	td button {
		font-size: 0.78rem;
		padding: 0 0.5rem;
		min-height: 0;
	}
	kbd {
		border: 1px solid var(--line);
		padding: 0 0.4em;
		color: var(--accent);
	}
	.small {
		font-size: 0.8rem;
		margin: 0;
	}
	p {
		margin: 0;
	}
</style>
