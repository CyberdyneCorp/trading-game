<script lang="ts">
	// A yes/no question before an action the mission asks the player to confirm (e.g. a stop moved past
	// liquidation). Escape or "cancel" leaves everything as it was.
	let {
		title,
		message,
		confirmLabel,
		cancelLabel,
		onConfirm,
		onCancel
	}: { title: string; message: string; confirmLabel: string; cancelLabel: string; onConfirm: () => void; onCancel: () => void } = $props();

	function onKey(e: KeyboardEvent) {
		if (e.key === 'Escape') onCancel();
	}
</script>

<svelte:window onkeydown={onKey} />

<div class="modal-backdrop" role="presentation">
	<div class="modal" role="alertdialog" aria-modal="true" aria-labelledby="confirm-title" aria-describedby="confirm-msg" data-testid="confirm-dialog">
		<h2 id="confirm-title">⚠ {title}</h2>
		<p id="confirm-msg">{message}</p>
		<div class="actions">
			<!-- svelte-ignore a11y_autofocus -->
			<button onclick={onCancel} autofocus>{cancelLabel}</button>
			<button class="primary" onclick={onConfirm} data-testid="confirm-yes">{confirmLabel}</button>
		</div>
	</div>
</div>

<style>
	.actions {
		display: flex;
		justify-content: flex-end;
		gap: 0.5rem;
	}
</style>
