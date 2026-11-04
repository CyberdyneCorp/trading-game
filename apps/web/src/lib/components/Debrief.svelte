<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { describeFinding, describeLesson } from '#lib/game/analysis.ts';
	import { fmtMoney, fmtPct, fmtPrice, num, signMark } from '#lib/game/format.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import Chart from './Chart.svelte';
	import { describeEvent } from './EventFeed.svelte';
	import type { EngineTrade } from '#lib/engine/types.ts';

	let { session }: { session: GameSession } = $props();
	const s = $derived(session.state!);
	const d = $derived(session.debrief!);
	const L = $derived(i18n.locale);
	// Engine report (authoritative; the server recomputes the same thing from the same commands).
	const a = $derived({ netPnl: Number(d.netPnl), dims: d.score.dims, total: d.score.total, findings: d.findings, lesson: d.lesson });
	const trades = $derived(d.trades);
	// "▲ COMPRA P 100 · exp 96" for option trades, "▲ LONG" for the perpetual.
	function tradeLabel(tr: EngineTrade): string {
		const arrow = tr.side === 'long' ? '▲' : '▼';
		if (!tr.series) return `${arrow} ${tr.side === 'long' ? 'LONG' : 'SHORT'}`;
		const series = tr.kind && tr.strike ? `${tr.kind === 'call' ? 'C' : 'P'} ${fmtPrice(tr.strike, L)} · exp ${tr.expiryStep}` : `OPT#${tr.series}`;
		return `${arrow} ${t(tr.side === 'long' ? 'trade.optBuy' : 'trade.optWrite')} ${series}`;
	}
	const cal = $derived({
		resolved: d.calibration.resolved,
		score: d.score.dims.calibration.score,
		brier: d.calibration.brierBp == null ? null : d.calibration.brierBp / 10000
	});
	const bankrupt = $derived(s.status === 'bankrupt');
	const hold = $derived(num(d.buyAndHoldPnl));
	const timeline = $derived(
		[
			...s.events
				.filter((e) => e.type !== 'order' || e.status !== 'accepted')
				.map((e) => ({ step: e.step, key: `e${e.seq}`, text: describeEvent(e, L, session.names)?.text ?? '', tone: describeEvent(e, L, session.names)?.tone ?? '' })),
			...session.journal.map((p, i) => ({
				step: p.step,
				key: `p${i}`,
				text: p.skipped
					? `✎ ${t('debrief.noPlan')}`
					: `✎ ${t('debrief.planLine', { thesis: p.thesis, inv: p.invalidation ?? '—', target: p.target ?? '—', conf: p.confidence })}`,
				tone: 'info'
			}))
		]
			.filter((x) => x.text)
			.sort((x, y) => x.step - y.step)
	);
	const dimLabels = { risk: 'debrief.risk', plan: 'debrief.plan', understanding: 'debrief.understanding', calibration: 'debrief.calibration' } as const;
</script>

<main class="debrief">
	{#if bankrupt}
		<section class="gameover" role="alert">
			<h1>{t('debrief.gameOver')}</h1>
			<p>{t('debrief.gameOverText')}</p>
		</section>
	{:else}
		<h1>{t('debrief.finished')}</h1>
	{/if}
	<p class="muted">{missionText(s.mission, L).title}</p>

	<div class="grid">
		<section class="panel">
			<h2 class="panel-title">{t('debrief.outcome')}</h2>
			<div class="big {a.netPnl >= 0 ? 'up' : 'down'}">{signMark(a.netPnl)} {fmtMoney(a.netPnl, L, true)}</div>
			<p class="muted">{fmtPct(a.netPnl / num(s.initialBalance), L, 2, true)} · {t('debrief.netPnl')}</p>
			<dl class="kv">
				<dt>{t('rail.realized')}</dt><dd>{fmtMoney(s.totals.realizedPnl, L, true)}</dd>
				<dt>{t('rail.fees')}</dt><dd>{fmtMoney(s.totals.feesPaid, L)}</dd>
				<dt>{t('rail.funding')}</dt><dd>{fmtMoney(s.totals.fundingNet, L, true)}</dd>
				{#if s.options.enabled}<dt>{t('opt.realized')}</dt><dd>{fmtMoney(s.totals.optionRealized, L, true)}</dd>{/if}
				<dt>{t('debrief.maxDrawdown')}</dt><dd>{fmtMoney(s.totals.maxDrawdown, L)}</dd>
				<dt>{t('debrief.peakExposure')}</dt><dd>{(s.totals.peakLeverageBp / 10000).toFixed(1)}x</dd>
				<dt>{t('debrief.vsFlat')}</dt><dd>{fmtMoney(0, L)}</dd>
				<dt>{t('debrief.vsHold')}</dt><dd class={hold >= 0 ? 'up' : 'down'}>{fmtMoney(hold, L, true)}</dd>
			</dl>
		</section>

		<section class="panel">
			<h2 class="panel-title">{t('debrief.score')}</h2>
			<div class="big">{a.total ?? '—'}<span class="muted small">/100</span></div>
			<dl class="kv">
				{#each Object.entries(a.dims) as [k, dim] (k)}
					<dt>{t(dimLabels[k as keyof typeof dimLabels])} <span class="muted">({dim.weight}%)</span></dt>
					<dd>{dim.score ?? t('debrief.na')}</dd>
				{/each}
			</dl>
			<p class="muted small">{t('debrief.scoreNote')}</p>
		</section>

		<section class="panel">
			<h2 class="panel-title">{t('debrief.reveal')}</h2>
			{#if d.reveal}
				<p data-testid="pack-reveal">
					{t('debrief.revealPack', {
						alias: d.alias,
						pair: d.reveal.pair || '—',
						asset: d.reveal.asset || '—',
						venue: d.reveal.venue || '—',
						from: d.reveal.periodFrom,
						to: d.reveal.periodTo,
						source: d.reveal.source
					})}
				</p>
				<p class="muted small">{t('debrief.revealTransform', { text: d.reveal.transformation })}</p>
				{#if d.reveal.gaps > 0}<p class="muted small">{t('debrief.revealGaps', { n: d.reveal.gaps })}</p>{/if}
			{:else}
				<p>{t('debrief.revealSynthetic', { alias: d.alias, seed: d.seed })}</p>
				<h3>{t('debrief.scenario')}</h3>
				<p class="reveal">{missionText(s.mission, L).reveal}</p>
			{/if}
			<p class="muted small">{fmtPrice(d.startPrice, L)} → {fmtPrice(d.endPrice, L)}</p>
		</section>
	</div>

	<section class="panel findings">
		<h2 class="panel-title">{t('debrief.findings')}</h2>
		<ul>
			{#each a.findings as f, i (i)}
				<li class={f.tone === 'bad' ? 'down' : f.tone === 'good' ? 'up' : f.tone === 'warn' ? 'attention' : 'info'}>
					{f.tone === 'good' ? '✔' : f.tone === 'bad' ? '✖' : f.tone === 'warn' ? '⚠' : 'ℹ'} {describeFinding(f, L)}
				</li>
			{/each}
		</ul>
		<p><strong>{t('debrief.nextLesson')}:</strong> <span class="info">{describeLesson(a.lesson, L)}</span></p>
		<p class="muted small">{t('debrief.disclaimer')}</p>
	</section>

	<section class="panel">
		<h2 class="panel-title">{t('debrief.trades')}</h2>
		{#if trades.length === 0}
			<p class="muted">{t('debrief.noTrades')}</p>
		{:else}
			<table class="trades">
				<thead>
					<tr>
						<th>#</th><th>{t('trade.side')}</th><th class="num">{t('trade.entry')}</th><th class="num">{t('trade.exit')}</th>
						<th class="num">{t('trade.net')}</th><th class="num">R</th><th class="num">{t('trade.bars')}</th>
						<th>{t('trade.reason')}</th><th>{t('trade.plan')}</th>
					</tr>
				</thead>
				<tbody>
					{#each trades as tr (tr.n)}
						{@const net = Number(tr.net)}
						{@const r = tr.rBp == null ? null : tr.rBp / 10000}
						<tr>
							<td>{tr.n}</td>
							<td class={tr.side === 'long' ? 'up' : 'down'}>{tradeLabel(tr)} × {Number(tr.qty)}</td>
							<td class="num">{fmtPrice(tr.entryAvg, L)} <span class="muted">@{tr.entryStep}</span></td>
							<td class="num">{Number(tr.exitAvg) > 0 ? fmtPrice(tr.exitAvg, L) : '—'}</td>
							<td class="num {net >= 0 ? 'up' : 'down'}">{signMark(net)} {fmtMoney(net, L, true)}</td>
							<td class="num">{r != null ? `${r >= 0 ? '+' : ''}${r.toFixed(2)}R` : '—'}</td>
							<td class="num">{tr.bars}</td>
							<td>{t(`trade.${tr.reason}`)}</td>
							<td class={tr.planRespected === false ? 'down' : tr.planRespected ? 'up' : 'muted'}>
								{tr.planRespected == null ? '—' : tr.planRespected ? '✔ ' + t('trade.respected') : '✖ ' + t('trade.violated')}
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{/if}
		<h3>{t('debrief.calibrationTitle')}</h3>
		<p class="muted">
			{cal.score != null
				? t('debrief.calibrationLine', { n: cal.resolved, brier: (cal.brier ?? 0).toFixed(3) })
				: t('debrief.calibrationNa')}
		</p>
	</section>

	<section class="panel chart-panel">
		<Chart game={s} />
	</section>

	<section class="panel">
		<h2 class="panel-title">{t('debrief.timeline')}</h2>
		<ol class="timeline">
			{#each timeline as row (row.key)}
				<li class={row.tone}><span class="muted">[{row.step}]</span> {row.text}</li>
			{/each}
		</ol>
	</section>

	<div class="actions">
		<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
		<button class="primary" onclick={() => session.startMission(s.mission)}>↻ {t('debrief.again')}</button>
	</div>
</main>

<style>
	.debrief {
		max-width: 1200px;
		margin: 0 auto;
		padding: 1.5rem 1rem 3rem;
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}
	.gameover {
		text-align: center;
		border: 2px solid var(--danger);
		padding: 1rem;
		background: rgba(255, 95, 122, 0.08);
	}
	.gameover h1 {
		color: var(--danger);
		font-size: 4rem;
		text-shadow: 0 0 14px rgba(255, 95, 122, 0.6);
		margin: 0;
	}
	.grid {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
		gap: 0.8rem;
	}
	.big {
		font-family: var(--font-pixel);
		font-size: 2.4rem;
	}
	.small {
		font-size: 0.8rem;
	}
	.findings ul {
		margin: 0;
		padding-left: 0.2rem;
		list-style: none;
		display: flex;
		flex-direction: column;
		gap: 0.35rem;
	}
	.trades {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
	}
	.trades th,
	.trades td {
		padding: 0.2rem 0.4rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.trades .num {
		text-align: right;
	}
	.reveal {
		border-left: 3px solid var(--attention);
		padding-left: 0.6rem;
	}
	.chart-panel {
		height: 380px;
	}
	.timeline {
		list-style: none;
		padding: 0;
		margin: 0;
		font-size: 0.82rem;
		max-height: 320px;
		overflow: auto;
	}
	.actions {
		display: flex;
		justify-content: space-between;
	}
</style>
