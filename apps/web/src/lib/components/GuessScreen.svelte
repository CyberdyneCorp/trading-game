<script lang="ts">
	// "Guess the next move" rounds (add-practice-and-advanced-strategies design D19). A round plays a
	// round market in its own engine worker (the session's run is never touched): the chart shows the
	// bars up to the decision only; after the commitment the next 12 bars are revealed and the decision
	// is scored (calibration, plan, consistency), never the P&L. One sample per round; the calibration
	// table appears after 20 rounds.
	import { onDestroy } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { EngineClient } from '#lib/engine/client.ts';
	import type { GameState } from '#lib/engine/types.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { fmtNum, fmtPrice } from '#lib/game/format.ts';
	import {
		GUESS_HORIZON,
		GUESS_TABLE_AFTER,
		calibrationTable,
		firstHit,
		fixed,
		pickRound,
		probabilities,
		scorePoints,
		scoreRound,
		type Commitment,
		type FirstHit,
		type GuessDecision,
		type GuessScore,
		type RoundPick
	} from '#lib/game/guessRound.ts';
	import Chart from './Chart.svelte';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);

	let engine: EngineClient | null = null;
	let pick = $state<RoundPick | null>(null);
	let atDecision = $state.raw<GameState | null>(null);
	let revealed = $state.raw<GameState | null>(null);
	let phase = $state<'intro' | 'loading' | 'commit' | 'reveal' | 'error'>('intro');
	let decision = $state<GuessDecision>('long');
	let p = $state(50);
	let inv = $state('');
	let target = $state('');
	let problem = $state('');
	let outcome = $state<{ score: GuessScore; hit: FirstHit; higher: boolean; from: string; to: string } | null>(null);

	const close = $derived(atDecision ? atDecision.bars[atDecision.step][3] : '0');
	const atr = $derived(atDecision ? (atDecision.indicators.atr14[atDecision.step] ?? '0') : '0');
	const rounds = $derived(session.progress.guessHistory);
	const table = $derived(calibrationTable(rounds.flatMap((r) => (r.higher == null ? [] : [{ p: r.p, higher: r.higher }]))));
	const decisionName = (d: GuessDecision) => t(d === 'long' ? 'guess.long' : d === 'short' ? 'guess.short' : 'guess.none');

	async function startRound(): Promise<void> {
		phase = 'loading';
		outcome = null;
		revealed = null;
		problem = '';
		inv = '';
		target = '';
		try {
			if (!engine) {
				engine = new EngineClient();
				await engine.request({ kind: 'init', engineUrl: session.practiceEngineUrl });
			}
			pick = pickRound(session.roundMarkets, session.today(), rounds.length);
			const market = session.roundMarkets.find((m) => m.id === pick!.market)!;
			await engine.request({ kind: 'start', missionId: pick.market, seed: pick.seed });
			atDecision = await engine.request({ kind: 'advance', steps: pick.step - market.warmupBars });
			phase = 'commit';
		} catch {
			phase = 'error';
		}
	}

	// A typed price with a comma or a dot, as the decimal string the score compares in fixed point.
	const typedDecimal = (v: string) => v.trim().replace(',', '.');

	function commitment(): Commitment {
		return decision === 'none' ? { decision, p } : { decision, p, invalidation: typedDecimal(inv), target: typedDecimal(target) };
	}

	async function commit(): Promise<void> {
		if (!engine || !atDecision || !pick) return;
		const c = commitment();
		if (decision !== 'none' && (fixed(typedDecimal(inv)) == null || fixed(typedDecimal(target)) == null)) {
			problem = t('guess.needLevels');
			return;
		}
		phase = 'loading';
		revealed = await engine.request({ kind: 'advance', steps: GUESS_HORIZON });
		const after = revealed.bars[pick.step + GUESS_HORIZON][3];
		const higher = (fixed(after) ?? 0n) > (fixed(close) ?? 0n);
		const score = scoreRound(c, { close, atr, higher });
		const next = revealed.bars.slice(pick.step + 1, pick.step + 1 + GUESS_HORIZON).map((b) => ({ high: b[1], low: b[2] }));
		outcome = { score, hit: firstHit(c, next), higher, from: close, to: after };
		session.saveGuess({ market: pick.market, seed: pick.seed, step: pick.step, decision, p, score: score.total, higher });
		phase = 'reveal';
	}

	onDestroy(() => engine?.dispose());
	const pts = (h: number) => fmtNum(scorePoints(h), L, h % 100 === 0 ? 0 : 2);
</script>

<main class="guess" data-testid="guess">
	<header class="row">
		<h1 class="glow">{t('guess.title')}</h1>
		<button onclick={() => session.backToMenu()} data-testid="guess-back">{t('practice.back')}</button>
	</header>

	{#if phase === 'intro'}
		<section class="panel">
			<p>{t('guess.intro')}</p>
			{#if session.roundMarkets.length === 0}
				<p class="attention">{t('guess.unsupported')}</p>
			{:else}
				<button class="primary" onclick={startRound} data-testid="guess-start">{t('guess.start')}</button>
			{/if}
		</section>
	{:else if phase === 'loading'}
		<p class="panel muted" role="status">{t('guess.loading')}</p>
	{:else if phase === 'error'}
		<p class="panel down" role="alert">{t('guess.unsupported')}</p>
	{/if}

	{#if (phase === 'commit' || phase === 'reveal') && atDecision && pick}
		<p class="muted small" data-testid="guess-market">{t('guess.market', { alias: atDecision.alias, step: pick.step })}</p>
		<section class="panel chart-box">
			<Chart game={phase === 'reveal' && revealed ? revealed : atDecision} moment={phase === 'reveal' ? pick.step : null} />
		</section>
	{/if}

	{#if phase === 'commit' && atDecision}
		<section class="panel" data-testid="guess-commit">
			<p data-testid="guess-close">{t('guess.close', { close: fmtPrice(close, L), atr: fmtPrice(atr, L) })}</p>
			<fieldset>
				<legend>{t('guess.decision')}</legend>
				{#each ['long', 'short', 'none'] as const as d (d)}
					<label><input type="radio" name="decision" value={d} bind:group={decision} data-testid="guess-{d}" /> {decisionName(d)}</label>
				{/each}
			</fieldset>
			<label class="block">
				{t('guess.p', { p })}
				<select bind:value={p} data-testid="guess-p">
					{#each probabilities() as v (v)}<option value={v}>{v}%</option>{/each}
				</select>
			</label>
			{#if decision !== 'none'}
				<div class="row">
					<label>{t('guess.invalidation')} <input inputmode="decimal" bind:value={inv} data-testid="guess-inv" /></label>
					<label>{t('guess.target')} <input inputmode="decimal" bind:value={target} data-testid="guess-target" /></label>
				</div>
			{/if}
			{#if problem}<p class="attention" role="alert">{problem}</p>{/if}
			<button class="primary" onclick={commit} data-testid="guess-commit-btn">{t('guess.commit')}</button>
		</section>
	{/if}

	{#if phase === 'reveal' && outcome}
		<section class="panel" data-testid="guess-reveal" aria-live="polite">
			<h2 class="panel-title">{t('guess.revealTitle')}</h2>
			<p>{t('guess.committed', { decision: decisionName(decision), p })}</p>
			{#if decision !== 'none'}<p class="muted small">{t('guess.levels', { inv: fmtPrice(inv, L), target: fmtPrice(target, L) })}</p>{/if}
			<p data-testid="guess-outcome">{t(outcome.higher ? 'guess.higher' : 'guess.lower', { from: fmtPrice(outcome.from, L), to: fmtPrice(outcome.to, L) })}</p>
			{#if decision !== 'none'}<p class="muted">{t(`guess.first.${outcome.hit}`)}</p>{/if}
			<p class="score" data-testid="guess-score">{t('guess.score', { score: pts(outcome.score.total) })}</p>
			<ul>
				<li>{t('guess.part.calibration', { v: pts(outcome.score.calibration) })}</li>
				<li>{t('guess.part.plan', { v: pts(outcome.score.plan) })}</li>
				{#if outcome.score.sideOk != null}
					<li class="sub">{outcome.score.sideOk ? '✓' : '✗'} {t('guess.check.side')}</li>
					<li class="sub">{outcome.score.distanceOk ? '✓' : '✗'} {t('guess.check.distance')}</li>
					<li class="sub">{outcome.score.rewardOk ? '✓' : '✗'} {t('guess.check.reward')}</li>
				{:else}
					<li class="sub">{t('guess.noTradePlan')}</li>
				{/if}
				<li>{t('guess.part.consistency', { v: pts(outcome.score.consistency) })}</li>
			</ul>
			<p class="muted small" data-testid="guess-one-sample">{t('guess.oneSample')}</p>
			<button class="primary" onclick={startRound} data-testid="guess-next">{t('guess.next')}</button>
		</section>
	{/if}

	<section class="panel" data-testid="guess-history">
		<p>{t('guess.rounds', { n: rounds.length })}</p>
		{#if rounds.length >= GUESS_TABLE_AFTER && table.length}
			<h2 class="panel-title">{t('guess.tableTitle')}</h2>
			<table data-testid="guess-table">
				<thead><tr><th>{t('guess.colBucket')}</th><th class="num">{t('guess.colRounds')}</th><th class="num">{t('guess.colStated')}</th><th class="num">{t('guess.colObserved')}</th></tr></thead>
				<tbody>
					{#each table as row (row.from)}
						<tr>
							<td>{row.from}–{row.from === 90 ? 100 : row.from + 10}%</td>
							<td class="num">{row.rounds}</td>
							<td class="num">{fmtNum(row.stated, L, 0)}%</td>
							<td class="num">{fmtNum(row.observed, L, 0)}%</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{:else}
			<p class="muted small">{t('guess.tableLocked', { n: GUESS_TABLE_AFTER, done: rounds.length })}</p>
		{/if}
	</section>
</main>

<style>
	.guess {
		max-width: 64rem;
		margin: 0 auto;
		padding: 1rem;
	}
	.row {
		display: flex;
		gap: 1rem;
		justify-content: space-between;
		align-items: center;
		flex-wrap: wrap;
	}
	.chart-box {
		height: 22rem;
		padding: 0;
	}
	fieldset {
		display: flex;
		gap: 1rem;
		flex-wrap: wrap;
		border: 1px solid var(--line, #333);
	}
	.block {
		display: block;
		margin: 0.75rem 0;
	}
	.score {
		font-family: var(--font-pixel);
		font-size: 1.4rem;
		color: var(--accent);
	}
	.sub {
		margin-left: 1rem;
		list-style: none;
	}
</style>
