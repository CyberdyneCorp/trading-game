<script lang="ts">
	// Daily challenge (add-practice-and-advanced-strategies design D16): today's mission and market, the
	// same for everyone on this local date; the player's own history only (first and best attempt by the
	// mission score, never by P&L). No leaderboard, no streak.
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { i18n, t } from '#lib/game/i18n.svelte.ts';
	import { missionText } from '#lib/game/missions.ts';
	import { fmtPrice } from '#lib/game/format.ts';
	import { dailyHistory } from '#lib/game/daily.ts';
	import type { DailyAttempt } from '#lib/game/storage.ts';

	let { session }: { session: GameSession } = $props();
	const L = $derived(i18n.locale);
	const today = $derived(session.dailyToday);
	const record = $derived(session.progress.dailyChallenges[today.date] ?? null);
	const history = $derived(dailyHistory(session.progress));

	function result(a: DailyAttempt | null): string {
		if (!a) return '—';
		return a.score == null ? `${t('daily.noScore')} · ${fmtPrice(a.pnl, L)} cr` : t('daily.result', { score: a.score, pnl: fmtPrice(a.pnl, L) });
	}
</script>

<main class="daily" data-testid="daily">
	<header class="row">
		<h1 class="glow">{t('daily.title')}</h1>
		<button onclick={() => session.backToMenu()} data-testid="daily-back">{t('practice.back')}</button>
	</header>
	<section class="panel">
		<p>{t('daily.intro')}</p>
		<h2 class="panel-title" data-testid="daily-today">{t('daily.today', { date: today.date })}</h2>
		<p data-testid="daily-mission"><strong>{missionText(today.mission, L).title}</strong> — {missionText(today.mission, L).tagline}</p>
		<p class="muted small" data-testid="daily-seed">{t('daily.market', { seed: today.seed })}</p>
		{#if record?.first}<p data-testid="daily-record">{t('daily.todayFirst', { result: result(record.first) })}</p>{/if}
		<button class="primary" onclick={() => session.startDaily()} disabled={session.busy} data-testid="daily-play">
			{record?.attempts ? t('daily.replay') : t('daily.play')}
		</button>
	</section>

	<section class="panel" data-testid="daily-history">
		<h2 class="panel-title">{t('daily.history')}</h2>
		{#if history.length === 0}
			<p class="muted">{t('daily.noHistory')}</p>
		{:else}
			<table>
				<thead>
					<tr><th>{t('daily.colDate')}</th><th>{t('daily.colMission')}</th><th>{t('daily.first')}</th><th>{t('daily.best')}</th><th class="num">{t('daily.attempts')}</th></tr>
				</thead>
				<tbody>
					{#each history as d (d.date)}
						<tr data-testid="daily-row">
							<td>{d.date}</td>
							<td>{missionText(d.mission, L).title}</td>
							<td data-testid="daily-first">{result(d.first)}</td>
							<td data-testid="daily-best">{result(d.best)}</td>
							<td class="num">{d.attempts}</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{/if}
	</section>
</main>

<style>
	.daily {
		max-width: 52rem;
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
	table {
		width: 100%;
	}
</style>
