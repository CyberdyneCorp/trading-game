<script lang="ts">
	// An incomplete combo (strategy-builder spec "Submission and partial failure"): some legs filled at the
	// open and others were cancelled. Names the open legs and the risk they carry now (from the engine's
	// what-if of the legs actually held) and offers to close them (after a confirmation) or keep them.
	// It never places or cancels anything on its own.
	import type { WhatIfLegInput, WhatIfResult } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { openLegs, type ComboStatus, type LegStatus } from '#lib/game/combos.ts';
	import { fmtMoney, fmtPrice } from '#lib/game/format.ts';
	import { i18n, reasonText, t } from '#lib/game/i18n.svelte.ts';
	import { unboundedAbove } from '#lib/game/whatifLab.ts';
	import ConfirmDialog from './ConfirmDialog.svelte';

	let { session, status, onDone }: { session: GameSession; status: ComboStatus; onDone: () => void } = $props();

	const L = $derived(i18n.locale);
	const open = $derived(openLegs(status));
	const cancelled = $derived(status.legs.filter((l) => l.state === 'cancelled'));
	let risk = $state<WhatIfResult | null>(null);
	let confirming = $state(false);

	function legInput(l: LegStatus): WhatIfLegInput {
		const base = { side: l.leg.side, qty: l.leg.qty, entry: l.price ?? undefined };
		return l.leg.series === 0 ? { kind: 'perp', ...base } : { series: l.leg.series, ...base };
	}

	$effect(() => {
		const legs = open.map(legInput);
		void session.runWhatIf({ legs }).then((r) => (risk = r.ok ? r.value : null));
	});

	function name(l: LegStatus): string {
		const q = session.state?.options.chain.find((x) => x.id === l.leg.series);
		const what = l.leg.series === 0 ? t('lab.perp') : q ? `${q.kind === 'call' ? 'C' : 'P'} ${fmtPrice(q.strike, L)}` : `OPT#${l.leg.series}`;
		return `${t(l.leg.side === 'buy' ? 'combo.long' : 'combo.short')} ${what}`;
	}

	const riskText = $derived.by(() => {
		if (!risk?.expiry) return null;
		const above = unboundedAbove(risk);
		if (above) return t('combo.riskUnbounded', { price: fmtPrice(above, L) });
		return t('combo.riskBounded', { loss: fmtMoney(risk.expiry.maxLoss ?? '0', L) });
	});

	async function closeOpen() {
		confirming = false;
		await session.sequence(async () => {
			for (const l of open) {
				const side = l.leg.side === 'buy' ? 'sell' : 'buy';
				if (l.leg.series === 0) await session.placeOrder('market', side, l.leg.qty, null, true);
				else await session.placeOptionOrder(l.leg.series, side, 'market', l.leg.qty, null);
			}
		});
		session.notice = { tone: 'ok', text: t('combo.closing') };
		onDone();
	}
</script>

<section class="panel combo-notice" role="alert" aria-labelledby="combo-notice-title" data-testid="combo-incomplete">
	<h2 id="combo-notice-title" class="panel-title">⚠ {t('combo.incompleteTitle', { name: t(`strategy.${status.record.strategy}`) })}</h2>
	<p>{t('combo.openLegs')}: <strong>{open.map(name).join(', ')}</strong></p>
	<p>{t('combo.cancelledLegs')}: {cancelled.map((l) => `${name(l)} (${reasonText(l.reason ?? 'none')})`).join(', ')}</p>
	{#if riskText}<p class="down" data-testid="combo-risk">{riskText}</p>{/if}
	<p class="muted small">{t('combo.legging')}</p>
	<div class="actions">
		<button onclick={onDone} data-testid="combo-keep">{t('combo.keep')}</button>
		<button class="primary" onclick={() => (confirming = true)} data-testid="combo-close">{t('combo.close')}</button>
	</div>
</section>
{#if confirming}
	<ConfirmDialog
		title={t('combo.closeTitle')}
		message={t('combo.closeConfirm', { legs: open.map(name).join(', ') })}
		confirmLabel={t('combo.closeYes')}
		cancelLabel={t('combo.keep')}
		onConfirm={() => void closeOpen()}
		onCancel={() => (confirming = false)}
	/>
{/if}

<style>
	.combo-notice {
		border-color: var(--attention);
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
		font-size: 0.85rem;
	}
	.combo-notice p {
		margin: 0;
	}
	.actions {
		display: flex;
		gap: 0.4rem;
		justify-content: flex-end;
	}
	.small {
		font-size: 0.75rem;
	}
</style>
