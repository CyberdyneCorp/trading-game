<script lang="ts">
	// Journal statistics (add-run-sharing-and-journal-stats design D7): the player's own runs that
	// reached a debrief, each number tied to the lesson it measures.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t, tCount, type MessageKey } from '#lib/game/i18n.svelte.ts';
	import { fmtNum, fmtPct } from '#lib/game/format.ts';
	import { journalStats, missingSummaries, PRACTICE_STAGE, R_BUCKETS, STAT_LESSONS, statsRuns, TREND_WINDOW, type CoreStats, type GroupRow } from '#lib/game/journalStats.ts';
	import { missionText, packOriginOf, packTextsOf } from '#lib/game/missions.ts';
	import { lessonMeta } from '#lib/content/academy.ts';
	import RunExport from './RunExport.svelte';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	let filter = $state('');
	let progress = $state<string | null>(null);
	const runs = $derived(statsRuns(session.journalRecords));
	const missions = $derived([...new Set(runs.map((r) => r.missionId))]);
	const stats = $derived(journalStats(runs, filter || null));
	const c = $derived(stats.core);
	const missing = $derived(missingSummaries(session.journalRecords).length);
	const maxBucket = $derived(Math.max(1, ...c.rBuckets));

	const pct = (v: number | null) => (v == null ? '—' : fmtPct(v, L, 0));
	const r = (v: number | null) => (v == null ? '—' : `${fmtNum(v, L, 2, true)}R`);
	const share = (n: number, d: number) => (d > 0 ? fmtPct(n / d, L, 0) : '—');
	function titleOf(missionId: string): string {
		const digest = session.journalRecords.find((x) => x.missionId === missionId)?.packDigest;
		return missionText(missionId, L, packOriginOf(session.packs, digest), packTextsOf(session.packs, digest)).title;
	}
	const stageName = (key: string) => (key === PRACTICE_STAGE ? t('stats.practice') : t(`stage.${key}` as MessageKey));

	async function recompute() {
		const res = await session.recomputeSummaries((done, total) => (progress = t('stats.recomputing', { done, total })));
		progress = t('stats.recomputed', res);
	}
</script>

{#snippet lesson(id: string)}
	{@const meta = lessonMeta(id)}
	{#if meta}
		<button class="link" onclick={() => session.openLesson(id, 'stats')} data-testid="stats-lesson">🎓 {t('stats.lesson', { title: meta.title[L] })}</button>
	{/if}
{/snippet}

{#snippet groups(title: string, rows: GroupRow[], name: (key: string) => string, testid: string)}
	<section class="panel" data-testid={testid}>
		<h2 class="panel-title">{title}</h2>
		<table>
			<thead>
				<tr><th>{t('stats.col.group')}</th><th class="num">{t('stats.col.runs')}</th><th class="num">{t('stats.col.trades')}</th><th class="num">{t('stats.col.winRate')}</th><th class="num">{t('stats.col.expectancy')}</th></tr>
			</thead>
			<tbody>
				{#each rows as g (g.key)}
					<tr><td>{name(g.key)}</td><td class="num">{g.runs}</td><td class="num">{g.trades}</td><td class="num">{pct(g.winRate)}</td><td class="num">{r(g.expectancyR)}</td></tr>
				{/each}
			</tbody>
		</table>
	</section>
{/snippet}

{#snippet compare(label: string, s: CoreStats | null)}
	<tr><th scope="row">{label}</th><td class="num">{s ? s.runs : '—'}</td><td class="num">{s ? r(s.expectancyR) : '—'}</td><td class="num">{s ? pct(s.respectedRate) : '—'}</td><td class="num">{s && s.avgRiskPct != null ? `${fmtNum(s.avgRiskPct, L, 2)}%` : '—'}</td></tr>
{/snippet}

<main class="stats" data-testid="stats">
	<header>
		<h1>{t('stats.title')}</h1>
		<button onclick={() => session.backToMenu()}>{t('debrief.menu')}</button>
	</header>
	<p class="muted">{t('stats.note')}</p>

	{#if missing > 0}
		<p class="panel" data-testid="stats-missing">
			{tCount('stats.missing', missing)}
			<button onclick={recompute} disabled={session.busy} data-testid="stats-recompute">{t('stats.recompute')}</button>
			{#if progress}<span class="muted" role="status">{progress}</span>{/if}
		</p>
	{/if}

	{#if runs.length === 0}
		<p class="panel muted" data-testid="stats-empty">{t('stats.empty')}</p>
	{:else}
		{#if missions.length > 1}
			<label class="filter">
				{t('journal.filter')}
				<select bind:value={filter} data-testid="stats-filter">
					<option value="">{t('journal.all')}</option>
					{#each missions as m (m)}<option value={m}>{titleOf(m)}</option>{/each}
				</select>
			</label>
		{/if}
		{#if stats.tooFew}<p class="attention" data-testid="stats-too-few">⚠ {tCount('stats.tooFew', c.trades)}</p>{/if}

		<div class="cards">
			<section class="panel" data-testid="stats-outcome">
				<h2 class="panel-title">{t('stats.outcome')}</h2>
				<dl class="kv">
					<dt>{t('stats.runs')}</dt><dd>{c.runs}</dd>
					<dt>{t('stats.trades')}</dt><dd data-testid="stats-trades">{c.trades}</dd>
					<dt>{t('stats.winRate')}</dt><dd data-testid="stats-win-rate">{pct(c.winRate)}</dd>
					<dt>{t('stats.expectancy')}</dt><dd data-testid="stats-expectancy">{r(c.expectancyR)}</dd>
					<dt>{t('stats.avgWin')}</dt><dd>{r(c.avgWinR)}</dd>
					<dt>{t('stats.avgLoss')}</dt><dd>{r(c.avgLossR)}</dd>
					<dt>{t('stats.withoutR')}</dt><dd>{c.withoutR}</dd>
				</dl>
				<p class="muted small">{t('stats.outcomeLesson')}</p>
				{@render lesson(STAT_LESSONS.outcome)}
			</section>

			<section class="panel" data-testid="stats-distribution">
				<h2 class="panel-title">{t('stats.distribution')}</h2>
				<ul class="bars">
					{#each R_BUCKETS as b, i (b)}
						<li>
							<span class="label">{t(`stats.bucket.${b}` as MessageKey)}</span>
							<span class="bar {i < 3 ? 'down' : 'up'}" style:width="{(c.rBuckets[i] / maxBucket) * 100}%" aria-hidden="true"></span>
							<span class="num">{c.rBuckets[i]}</span>
						</li>
					{/each}
				</ul>
			</section>

			<section class="panel" data-testid="stats-stops">
				<h2 class="panel-title">{t('stats.stops')}</h2>
				<dl class="kv">
					<dt>{t('stats.respected')}</dt><dd data-testid="stats-respected">{share(c.stopsRespected, c.stopsJudged)} <span class="muted">({c.stopsRespected}/{c.stopsJudged})</span></dd>
					<dt>{t('stats.movedAway')}</dt><dd class:down={c.movedAway > 0} data-testid="stats-moved">{share(c.movedAway, c.stopsReported)} <span class="muted">({c.movedAway}/{c.stopsReported})</span></dd>
				</dl>
				<p class="muted small">{t('stats.stopsLesson')}</p>
				{@render lesson(STAT_LESSONS.stops)}
			</section>

			<section class="panel" data-testid="stats-risk">
				<h2 class="panel-title">{t('stats.risk')}</h2>
				<dl class="kv">
					<dt>{t('stats.avgRisk')}</dt><dd data-testid="stats-avg-risk">{c.avgRiskPct == null ? '—' : `${fmtNum(c.avgRiskPct, L, 2)}%`}</dd>
					<dt>{t('stats.below1')}</dt><dd>{share(c.riskBelow1, c.riskTrades)}</dd>
					<dt>{t('stats.within')}</dt><dd class="up">{share(c.risk1to2, c.riskTrades)}</dd>
					<dt>{t('stats.above2')}</dt><dd class:down={c.riskAbove2 > 0}>{share(c.riskAbove2, c.riskTrades)}</dd>
					<dt>{t('stats.oversizedRuns')}</dt><dd>{c.oversizedRuns}</dd>
				</dl>
				<p class="muted small">{t('stats.riskLesson')}</p>
				{@render lesson(STAT_LESSONS.risk)}
			</section>

			<section class="panel" data-testid="stats-liquidations">
				<h2 class="panel-title">{t('stats.liquidations')}</h2>
				<dl class="kv">
					<dt>{t('stats.liq')}</dt><dd class:down={c.liquidations > 0}>{c.liquidations}</dd>
					<dt>{t('stats.marginCalls')}</dt><dd class:down={c.marginCalls > 0}>{c.marginCalls}</dd>
				</dl>
				<p class="muted small">{t('stats.liqLesson')}</p>
				{@render lesson(STAT_LESSONS.liquidations)}
				{#if c.marginCalls > 0}{@render lesson(STAT_LESSONS.marginCalls)}{/if}
			</section>
		</div>

		<section class="panel" data-testid="stats-mistakes">
			<h2 class="panel-title">{t('stats.mistakes')}</h2>
			{#if stats.mistakes.length === 0}
				<p class="muted">{t('stats.mistakesNone')}</p>
			{:else}
				<ul class="mistakes">
					{#each stats.mistakes as m (m.code)}
						<li class={m.tone === 'bad' ? 'down' : 'attention'}>
							{t(`statsFinding.${m.code}` as MessageKey)} <span class="muted">{tCount('stats.mistakeRuns', m.runs)}</span>
							{#if m.repeats}<span class="tag attention">{t('stats.repeats')}</span>{/if}
							{#if m.lesson}{@render lesson(m.lesson)}{/if}
						</li>
					{/each}
				</ul>
			{/if}
		</section>

		<div class="cards two">
			{@render groups(t('stats.byMission'), stats.byMission, titleOf, 'stats-by-mission')}
			{@render groups(t('stats.byStage'), stats.byStage, stageName, 'stats-by-stage')}
		</div>

		<section class="panel" data-testid="stats-trend">
			<h2 class="panel-title">{t('stats.trend')}</h2>
			{#if stats.trend.previous}
				<p class="muted small">{t('stats.trendNote', { n: TREND_WINDOW })}</p>
				<table>
					<thead><tr><th></th><th class="num">{t('stats.col.runs')}</th><th class="num">{t('stats.col.expectancy')}</th><th class="num">{t('stats.col.respected')}</th><th class="num">{t('stats.avgRisk')}</th></tr></thead>
					<tbody>
						{@render compare(t('stats.recent'), stats.trend.recent)}
						{@render compare(t('stats.previous'), stats.trend.previous)}
					</tbody>
				</table>
			{:else}
				<p class="muted">{t('stats.trendFew')}</p>
			{/if}
			<h3>{t('stats.runsTable')}</h3>
			<table data-testid="stats-runs">
				<thead><tr><th>{t('stats.col.date')}</th><th>{t('stats.col.mission')}</th><th class="num">{t('stats.col.trades')}</th><th class="num">{t('stats.col.expectancy')}</th><th class="num">{t('stats.col.score')}</th><th></th></tr></thead>
				<tbody>
					{#each [...stats.trend.points].reverse() as p (p.runId)}
						<tr>
							<td>{new Date(p.createdAt).toLocaleDateString(L)}</td>
							<td>{titleOf(p.missionId)}</td>
							<td class="num">{p.trades}</td>
							<td class="num">{r(p.expectancyR)}</td>
							<td class="num">{p.score ?? '—'}</td>
							<td class="actions">
								<button onclick={() => session.watchOwnRun(p.runId, 'stats')} data-testid="stats-watch">▶ {t('journal.watchOwn')}</button>
								<RunExport {session} runId={p.runId} compact />
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
		</section>
	{/if}
</main>

<style>
	.stats {
		max-width: 1100px;
		margin: 0 auto;
		padding: 2rem 1rem;
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}
	header {
		display: flex;
		justify-content: space-between;
		align-items: center;
	}
	h1 {
		margin: 0;
	}
	.cards {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
		gap: 0.8rem;
	}
	.cards.two {
		grid-template-columns: repeat(auto-fit, minmax(420px, 1fr));
	}
	.bars {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
	}
	.bars li {
		display: grid;
		grid-template-columns: 7rem 1fr 2.5rem;
		gap: 0.4rem;
		align-items: center;
	}
	.bar {
		display: block;
		height: 0.8rem;
		min-width: 1px;
		background: currentColor;
	}
	.mistakes {
		margin: 0;
		padding-left: 1.2rem;
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
	}
	table {
		width: 100%;
		border-collapse: collapse;
	}
	th,
	td {
		text-align: left;
		padding: 0.2rem 0.4rem;
	}
	.num {
		text-align: right;
	}
	.actions {
		display: flex;
		gap: 0.4rem;
		align-items: center;
	}
	.filter {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}
	.filter select {
		width: auto;
	}
	.link {
		border: none;
		background: none;
		padding: 0;
		text-decoration: underline;
		color: var(--info, inherit);
		cursor: pointer;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
