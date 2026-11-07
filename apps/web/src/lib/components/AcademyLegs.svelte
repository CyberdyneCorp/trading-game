<script lang="ts">
	// Leg editor of the Academy lab: kind, side, strike, contracts, days to expiry, IV and the premium paid
	// or received, on an illustrative market. Up to four option legs and one perpetual leg.
	import { inputText, parseDecimal } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { MAX_OPTION_LEGS } from '#lib/game/strategies.ts';
	import { ACADEMY_FORWARD, type AcademyLeg } from '#lib/game/whatifLab.ts';

	let { legs = $bindable<AcademyLeg[]>([]) }: { legs?: AcademyLeg[] } = $props();

	const options = $derived(legs.filter((l) => l.kind !== 'perp').length);
	const hasPerp = $derived(legs.some((l) => l.kind === 'perp'));

	function update(i: number, patch: Partial<AcademyLeg>) {
		legs = legs.map((l, j) => (j === i ? { ...l, ...patch } : l));
	}
	function num(e: Event, min = 0): number {
		const v = parseDecimal((e.currentTarget as HTMLInputElement).value);
		return Number.isFinite(v) && v >= min ? v : min;
	}
	function addOption() {
		legs = [...legs, { kind: 'call', side: 'buy', strike: ACADEMY_FORWARD, qty: 1, days: 30, ivPct: 60, entry: 0 }];
	}
	function addPerp() {
		legs = [...legs, { kind: 'perp', side: 'buy', strike: 0, qty: 1, days: 0, ivPct: 0, entry: ACADEMY_FORWARD }];
	}
</script>

<table class="legs" data-testid="lab-legs">
	<caption class="muted small">{t('lab.legsCaption')}</caption>
	<thead>
		<tr>
			<th scope="col">{t('lab.kind')}</th><th scope="col">{t('lab.side')}</th><th scope="col">{t('opt.strike')}</th>
			<th scope="col">{t('opt.qty')}</th><th scope="col">{t('lab.daysToExpiry')}</th><th scope="col">{t('lab.ivPct')}</th>
			<th scope="col">{t('lab.entry')}</th><th scope="col"><span class="sr-only">{t('lab.remove')}</span></th>
		</tr>
	</thead>
	<tbody>
		{#each legs as l, i (i)}
			<tr data-testid="lab-leg">
				<td>
					{#if l.kind === 'perp'}{t('lab.perp')}{:else}
						<select aria-label={t('lab.kind')} value={l.kind} onchange={(e) => update(i, { kind: (e.currentTarget as HTMLSelectElement).value as 'call' | 'put' })}>
							<option value="call">Call</option><option value="put">Put</option>
						</select>
					{/if}
				</td>
				<td>
					<select aria-label={t('lab.side')} value={l.side} onchange={(e) => update(i, { side: (e.currentTarget as HTMLSelectElement).value as 'buy' | 'sell' })}>
						<option value="buy">{t('opt.buy')}</option><option value="sell">{t('opt.sell')}</option>
					</select>
				</td>
				<td>{#if l.kind !== 'perp'}<input aria-label={t('opt.strike')} inputmode="decimal" value={inputText(l.strike, i18n.locale)} onchange={(e) => update(i, { strike: num(e, 0.01) })} />{/if}</td>
				<td><input aria-label={t('opt.qty')} inputmode="decimal" value={l.qty} onchange={(e) => update(i, { qty: num(e, 1) })} /></td>
				<td>{#if l.kind !== 'perp'}<input aria-label={t('lab.daysToExpiry')} inputmode="numeric" value={l.days} onchange={(e) => update(i, { days: Math.round(num(e)) })} />{/if}</td>
				<td>{#if l.kind !== 'perp'}<input aria-label={t('lab.ivPct')} inputmode="decimal" value={inputText(l.ivPct, i18n.locale)} onchange={(e) => update(i, { ivPct: num(e, 1) })} />{/if}</td>
				<td><input aria-label={t('lab.entry')} inputmode="decimal" value={inputText(l.entry, i18n.locale)} onchange={(e) => update(i, { entry: num(e) })} /></td>
				<td><button aria-label={t('lab.remove')} onclick={() => (legs = legs.filter((_, j) => j !== i))} disabled={legs.length <= 1}>✕</button></td>
			</tr>
		{/each}
	</tbody>
</table>
<div class="add">
	<button onclick={addOption} disabled={options >= MAX_OPTION_LEGS} data-testid="lab-add-option">+ {t('lab.addOption')}</button>
	<button onclick={addPerp} disabled={hasPerp}>+ {t('lab.addPerp')}</button>
</div>

<style>
	.legs {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.78rem;
	}
	th,
	td {
		padding: 0.1rem 0.2rem;
		text-align: left;
	}
	input {
		width: 5rem;
	}
	.add {
		display: flex;
		gap: 0.4rem;
	}
	.small {
		font-size: 0.75rem;
	}
	.sr-only {
		position: absolute;
		width: 1px;
		height: 1px;
		overflow: hidden;
		clip: rect(0 0 0 0);
	}
</style>
