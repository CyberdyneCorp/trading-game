<script lang="ts">
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { describeFindings, describeLesson } from '#lib/game/analysis.ts';
	import { pctFromBp } from '#lib/game/advisor.ts';
	import RegimeReveal from './RegimeReveal.svelte';
	import { entryRegimeName, fmtLeverageBp, fmtLiqDistance, greekRows, newsTimeline, openPnl, revealTexts, stopAdvice } from '#lib/game/debrief.ts';
	import { fmtMoney, fmtNum, fmtPct, fmtPrice, fmtQty, num, signMark } from '#lib/game/format.ts';
	import { i18n, t, tCount } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { perksUsed } from '#lib/game/perks.ts';
	import CampaignOutcome from './CampaignOutcome.svelte';
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
	const greeks = $derived(greekRows(d.greeks));
	const findingTexts = $derived(describeFindings(a.findings, L));
	// Depends on L so the texts follow a language switch.
	const reveal = $derived(d.reveal && L ? revealTexts(d.reveal, d.alias) : null);
	// The run can end (game over) before the scenario does; the market line and buy-and-hold stop with it.
	const endedEarly = $derived(s.step < s.totalBars - 1);
	function adviceText(tr: EngineTrade): string | null {
		const adv = stopAdvice(tr, L);
		if (!adv) return null;
		return t(adv.lev > 0 ? 'trade.stopBeyond' : 'trade.stopBeyondNone', { stop: adv.stop, liq: adv.liq, lev: adv.lev, used: adv.used });
	}
	// "▲ COMPRA P 100 · exp 96" for option trades, "▲ LONG" for the perpetual.
	function tradeLabel(tr: EngineTrade): string {
		const arrow = tr.side === 'long' ? '▲' : '▼';
		if (!tr.series) return `${arrow} ${tr.side === 'long' ? 'LONG' : 'SHORT'}`;
		const series = tr.kind && tr.strike ? `${tr.kind === 'call' ? 'C' : 'P'} ${fmtPrice(tr.strike, L)} · exp ${tr.expiryStep}` : `OPT#${tr.series}`;
		return `${arrow} ${t(tr.side === 'long' ? 'trade.optBuy' : 'trade.optWrite')} ${series}`;
	}
	// Old journal entries may hold NaN (a decimal comma read before the fix): shown as no level.
	const planLevel = (v: number | null) => (v != null && Number.isFinite(v) ? fmtPrice(String(v), L) : '—');
	const cal = $derived({
		resolved: d.calibration.resolved,
		score: d.score.dims.calibration.score,
		brier: d.calibration.brierBp == null ? null : d.calibration.brierBp / 10000
	});
	const bankrupt = $derived(s.status === 'bankrupt');
	const hold = $derived(num(d.buyAndHoldPnl));
	// Still open at the end: part of the net result, so the outcome lines add up to it.
	const openAtEnd = $derived(openPnl(s.account));
	const timeline = $derived(
		[
			...s.events
				.filter((e) => e.type !== 'order' || e.status !== 'accepted' || e.reason === 'order_moved')
				.map((e) => ({ step: e.step, key: `e${e.seq}`, text: describeEvent(e, L, session.names)?.text ?? '', tone: describeEvent(e, L, session.names)?.tone ?? '' })),
			...session.journal.map((p, i) => ({
				step: p.step,
				key: `p${i}`,
				text: p.skipped
					? `✎ ${t('debrief.noPlan')}`
					: `✎ ${t('debrief.planLine', { thesis: p.thesis, inv: planLevel(p.invalidation), target: planLevel(p.target), conf: p.confidence })}`,
				tone: 'info'
			})),
			...newsTimeline(s.news, L)
		]
			.filter((x) => x.text)
			.sort((x, y) => x.step - y.step)
	);
	const intel = $derived(perksUsed(session.options));
	const points = $derived({ used: s.advisor.advice.length, earned: d.advisorPointsEarned, balance: session.progress.advisorPoints });
	const judged = $derived(d.advice.filter((x) => x.outcome === 'right' || x.outcome === 'wrong').length);
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
	{#if intel.length}
		<p class="intel" data-testid="debrief-intel">
			<span class="tag attention">{t('debrief.intel')}</span>
			{t('debrief.intelNote', { perks: intel.map((p) => t(`perk.${p}`)).join(', ') })}
		</p>
	{/if}
	<CampaignOutcome {session} />

	<div class="grid">
		<section class="panel">
			<h2 class="panel-title">{t('debrief.outcome')}</h2>
			<div class="big {a.netPnl >= 0 ? 'up' : 'down'}">{signMark(a.netPnl)} {fmtMoney(a.netPnl, L, true)}</div>
			<p class="muted">{fmtPct(a.netPnl / num(s.initialBalance), L, 2, true)} · {t('debrief.netPnl')}</p>
			<dl class="kv">
				<dt>{t('rail.realized')}</dt><dd>{fmtMoney(s.totals.realizedPnl, L, true)}</dd>
				{#if openAtEnd !== 0}
					<dt>{t('debrief.openPnl')}</dt><dd class={openAtEnd >= 0 ? 'up' : 'down'} data-testid="debrief-open">{fmtMoney(openAtEnd, L, true)}</dd>
				{/if}
				<dt>{t('rail.fees')}</dt><dd>{fmtMoney(s.totals.feesPaid, L)}</dd>
				<dt>{t('rail.funding')}</dt><dd>{fmtMoney(s.totals.fundingNet, L, true)}</dd>
				<dt>{t('debrief.spread')}</dt><dd data-testid="debrief-spread">{fmtMoney(d.spreadSlippage, L)}</dd>
				{#if s.options.enabled}<dt>{t('opt.realized')}</dt><dd>{fmtMoney(s.totals.optionRealized, L, true)}</dd>{/if}
				{#if num(d.writtenOff) > 0}
					<dt>{t('debrief.writtenOff')}</dt><dd class="up" data-testid="debrief-written-off">{fmtMoney(d.writtenOff, L, true)}</dd>
				{/if}
				<dt>{t('debrief.maxDrawdown')}</dt><dd>{fmtMoney(s.totals.maxDrawdown, L)}</dd>
				<dt>{t('debrief.peakExposure')}</dt><dd>{fmtLeverageBp(s.totals.peakLeverageBp, L)}</dd>
				<dt>{t('debrief.minLiqDistance')}</dt><dd data-testid="debrief-liq-distance">{fmtLiqDistance(s.totals.minLiqDistanceBp, L)}</dd>
				<dt>{t('debrief.turnover')}</dt><dd data-testid="debrief-turnover">{fmtMoney(s.totals.turnover, L)}</dd>
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
				<p data-testid="pack-reveal">{reveal?.line}</p>
				<p class="muted small">{t('debrief.revealTransform', { text: reveal?.transform ?? '' })}</p>
				{#if d.reveal.gaps > 0}<p class="muted small">{tCount('debrief.revealGaps', d.reveal.gaps)}</p>{/if}
			{:else}
				<p>{t('debrief.revealSynthetic', { alias: d.alias, seed: d.seed })}</p>
				<h3>{t('debrief.scenario')}</h3>
				<p class="reveal">{missionText(s.mission, L).reveal}</p>
			{/if}
			<p class="muted small" data-testid="market-span">{t('debrief.marketSpan', { from: fmtPrice(d.startPrice, L), to: fmtPrice(d.endPrice, L) })}</p>
			{#if endedEarly}
				<p class="muted small" data-testid="ended-early">{t('debrief.endedEarly', { step: s.step, end: s.totalBars - 1 })}</p>
			{/if}
		</section>
	</div>

	<section class="panel findings">
		<h2 class="panel-title">{t('debrief.findings')}</h2>
		<ul>
			{#each a.findings as f, i (i)}
				<li class={f.tone === 'bad' ? 'down' : f.tone === 'good' ? 'up' : f.tone === 'warn' ? 'attention' : 'info'}>
					{f.tone === 'good' ? '✔' : f.tone === 'bad' ? '✖' : f.tone === 'warn' ? '⚠' : 'ℹ'} {findingTexts[i]}
				</li>
			{/each}
		</ul>
		<p><strong>{t('debrief.nextLesson')}:</strong> <span class="info">{describeLesson(a.lesson, L)}</span></p>
		<p class="muted small">{t('debrief.disclaimer')}</p>
	</section>

	{#if d.greeks}
		<section class="panel" data-testid="debrief-greeks">
			<h2 class="panel-title">{t('debrief.greeksTitle')}</h2>
			<dl class="kv">
				{#each greeks as g (g.key)}
					<dt>{t(`greek.${g.key}`)}</dt><dd class={g.value >= 0 ? 'up' : 'down'}>{signMark(g.value)} {fmtMoney(g.value, L, true)}</dd>
				{/each}
				<dt><strong>{t('greek.total')}</strong></dt><dd><strong>{fmtMoney(d.greeks.total, L, true)}</strong></dd>
			</dl>
			<p class="muted small">{t('debrief.greeksNote')}</p>
		</section>
	{/if}

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
						{#if session.reveal}<th>{t('trade.regime')}</th>{/if}
					</tr>
				</thead>
				<tbody>
					{#each trades as tr (tr.n)}
						{@const net = Number(tr.net)}
						{@const r = tr.rBp == null ? null : tr.rBp / 10000}
						{@const advice = adviceText(tr)}
						<tr>
							<td>{tr.n}</td>
							<td class={tr.side === 'long' ? 'up' : 'down'}>{tradeLabel(tr)} × {fmtQty(tr.qty, L)}</td>
							<td class="num">{fmtPrice(tr.entryAvg, L)} <span class="muted">@{tr.entryStep}</span></td>
							<td class="num">{tr.exitStep != null ? fmtPrice(tr.exitAvg, L) : '—'}</td>
							<td class="num {net >= 0 ? 'up' : 'down'}">{signMark(net)} {fmtMoney(net, L, true)}</td>
							<td class="num">{r != null ? `${fmtNum(r, L, 2, true)}R` : '—'}</td>
							<td class="num">{tr.bars}</td>
							<td>{t(`trade.${tr.reason}`)}</td>
							<td class={tr.planRespected === false ? 'down' : tr.planRespected ? 'up' : 'muted'}>
								{tr.planRespected == null ? '—' : tr.planRespected ? '✔ ' + t('trade.respected') : '✖ ' + t('trade.violated')}
							</td>
							{#if session.reveal}<td data-testid="trade-regime">{entryRegimeName(tr, session.reveal.spans)}</td>{/if}
						</tr>
						{#if advice}
							<tr class="advice"><td></td><td colspan={session.reveal ? 9 : 8} class="down">✖ {advice}</td></tr>
						{/if}
					{/each}
				</tbody>
			</table>
		{/if}
		<h3>{t('debrief.calibrationTitle')}</h3>
		<p class="muted">
			{cal.score != null
				? tCount('debrief.calibrationLine', cal.resolved, { brier: fmtNum(cal.brier ?? 0, L, 3) })
				: t('debrief.calibrationNa')}
		</p>
	</section>

	{#if !s.knownScenario}
		<section class="panel" data-testid="debrief-advisor">
			<h2 class="panel-title">{t('debrief.advisor')}</h2>
			{#if d.advice.length === 0}
				<p class="muted">{t('debrief.adviceNone')}</p>
			{:else}
				<table class="trades">
					<thead>
						<tr>
							<th>{t('debrief.adviceStep')}</th><th>{t('debrief.adviceStance')}</th>
							<th class="num">{t('debrief.adviceConfidence')}</th><th>{t('debrief.adviceOutcome')}</th>
						</tr>
					</thead>
					<tbody>
						{#each d.advice as row, i (i)}
							<tr>
								<td>{row.step}</td>
								<td class={row.stance === 'long' ? 'up' : row.stance === 'short' ? 'down' : 'attention'}>{t(`adv.stance.${row.stance}`)}</td>
								<td class="num">{pctFromBp(row.confidenceBp)}%</td>
								<td class={row.outcome === 'right' ? 'up' : row.outcome === 'wrong' ? 'down' : 'muted'}>{t(`debrief.outcome.${row.outcome}`)}</td>
							</tr>
						{/each}
					</tbody>
				</table>
				{#if d.adviceHitBp != null}
					<p class="muted">{t('debrief.adviceHit', { pct: pctFromBp(d.adviceHitBp), n: judged })}</p>
				{/if}
			{/if}
			<p data-testid="debrief-points">{t('debrief.points', points)}</p>
			<p class="muted small">{t('debrief.pointsHow')}</p>
		</section>
	{/if}

	<section class="panel chart-panel">
		<Chart game={s} />
	</section>
	{#if session.reveal}
		<RegimeReveal spans={session.reveal.spans} rows={d.regimes} from={s.warmupBars} to={s.step} />
	{/if}

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
		{#if !session.runCampaign}
			<button class="primary" onclick={() => session.startMission(s.mission)}>↻ {t('debrief.again')}</button>
		{/if}
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
