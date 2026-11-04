<script lang="ts">
	// Chain at the current decision step only (spec terminal-ui "Option chain"). Prices are model-made.
	import type { GameState, OptionQuoteView } from '#lib/engine/types.ts';
	import { fmtNum, fmtPct, fmtPrice } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';

	let {
		game,
		selected,
		onPick
	}: { game: GameState; selected: number | null; onPick: (q: OptionQuoteView, side: 'buy' | 'sell') => void } = $props();

	const L = $derived(i18n.locale);
	const expiries = $derived([...new Set(game.options.chain.map((q) => q.expiryStep))].sort((a, b) => a - b));
	const mark = $derived(Number(game.account.mark));

	function rows(expiry: number) {
		const strikes = [...new Set(game.options.chain.filter((q) => q.expiryStep === expiry).map((q) => q.strike))].sort(
			(a, b) => Number(a) - Number(b)
		);
		return strikes.map((k) => ({
			strike: k,
			call: game.options.chain.find((q) => q.expiryStep === expiry && q.strike === k && q.kind === 'call'),
			put: game.options.chain.find((q) => q.expiryStep === expiry && q.strike === k && q.kind === 'put')
		}));
	}
	const held = (id: number | undefined) => game.options.positions.some((p) => p.series === id);
</script>

<section class="panel chain" aria-label={t('opt.chain')}>
	<h2 class="panel-title">{t('opt.chain')} <span class="tag attention">{t('opt.simulated')}</span></h2>
	{#if game.options.chain.length === 0}
		<p class="muted">—</p>
	{/if}
	{#each expiries as exp (exp)}
		<p class="muted small">{t('opt.expiry', { step: exp, n: exp - game.step })}</p>
		<table>
			<thead>
				<tr>
					<th colspan="4" class="up">{t('opt.calls')}</th>
					<th>{t('opt.strike')}</th>
					<th colspan="4" class="down">{t('opt.puts')}</th>
				</tr>
				<tr class="muted">
					<th>{t('opt.delta')}</th><th>{t('opt.iv')}</th><th>{t('opt.bid')}</th><th>{t('opt.ask')}</th>
					<th></th>
					<th>{t('opt.bid')}</th><th>{t('opt.ask')}</th><th>{t('opt.iv')}</th><th>{t('opt.delta')}</th>
				</tr>
			</thead>
			<tbody>
				{#each rows(exp) as r (r.strike)}
					{@const itmCall = mark > Number(r.strike)}
					<tr>
						{#if r.call}
							<td class="num muted">{fmtNum(Number(r.call.delta), L)}</td>
							<td class="num muted">{fmtPct(Number(r.call.iv), L, 0)}</td>
							<td class="num"><button class="q" class:sel={selected === r.call.id} onclick={() => onPick(r.call!, 'sell')}>{fmtPrice(r.call.bid, L)}</button></td>
							<td class="num"><button class="q" class:sel={selected === r.call.id} onclick={() => onPick(r.call!, 'buy')}>{fmtPrice(r.call.ask, L)}</button></td>
						{:else}<td colspan="4"></td>{/if}
						<td class="strike" class:itm={itmCall}>{fmtPrice(r.strike, L)}{held(r.call?.id) || held(r.put?.id) ? ' ●' : ''}</td>
						{#if r.put}
							<td class="num"><button class="q" class:sel={selected === r.put.id} onclick={() => onPick(r.put!, 'sell')}>{fmtPrice(r.put.bid, L)}</button></td>
							<td class="num"><button class="q" class:sel={selected === r.put.id} onclick={() => onPick(r.put!, 'buy')}>{fmtPrice(r.put.ask, L)}</button></td>
							<td class="num muted">{fmtPct(Number(r.put.iv), L, 0)}</td>
							<td class="num muted">{fmtNum(Number(r.put.delta), L)}</td>
						{:else}<td colspan="4"></td>{/if}
					</tr>
				{/each}
			</tbody>
		</table>
	{/each}
	<p class="muted small">{t('opt.pick')}</p>
</section>

<style>
	.chain {
		overflow: auto;
	}
	table {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.78rem;
		margin-bottom: 0.4rem;
	}
	th,
	td {
		padding: 0.1rem 0.25rem;
		border-bottom: 1px solid var(--line);
		text-align: center;
	}
	.strike {
		font-weight: 600;
		color: var(--text);
		background: var(--panel-2);
	}
	.strike.itm {
		color: var(--info);
	}
	.q {
		background: none;
		border: 1px solid transparent;
		padding: 0 0.3rem;
		min-height: 22px;
		font-size: 0.78rem;
		font-variant-numeric: tabular-nums;
	}
	.q:hover,
	.q.sel {
		border-color: var(--accent);
	}
	.small {
		font-size: 0.75rem;
		margin: 0.2rem 0;
	}
</style>
