<script lang="ts">
	// A mistake retry's debrief (add-practice-and-advanced-strategies design D18): repeated or avoided
	// (the same code at a step from the moment's to five later), and both results side by side, labeled
	// one sample.
	import { i18n, t, type MessageKey } from '#lib/game/i18n.svelte.ts';
	import { fmtPrice } from '#lib/game/format.ts';
	import { REPEAT_WINDOW, mistakeName } from '#lib/game/coachReview.ts';
	import type { MistakeReport } from '#lib/game/practice.ts';

	let { report }: { report: MistakeReport } = $props();
	const L = $derived(i18n.locale);
	const name = $derived(t(mistakeName(report.code)));
	const key: MessageKey = $derived(report.outcome === 'repeated' ? 'mistake.repeated' : 'mistake.avoided');
	const score = (v: number | null) => (v == null ? '—' : String(v));
</script>

<section class="panel mistake" data-testid="mistake-report" data-outcome={report.outcome}>
	<h2 class="panel-title">{t('mistake.title')}</h2>
	<p class={report.outcome === 'repeated' ? 'attention' : 'up'} data-testid="mistake-outcome">
		{t(key, { moment: name, from: report.step, to: report.step + REPEAT_WINDOW })}
	</p>
	<table>
		<thead><tr><th></th><th class="num">{t('mistake.colScore')}</th><th class="num">{t('mistake.colPnl')}</th></tr></thead>
		<tbody>
			<tr>
				<th scope="row">{t('mistake.original')}</th>
				<td class="num">{score(report.original?.score ?? null)}</td>
				<td class="num">{report.original ? `${fmtPrice(report.original.pnl, L)} cr` : '—'}</td>
			</tr>
			<tr>
				<th scope="row">{t('mistake.retryRun')}</th>
				<td class="num">{score(report.retry.score)}</td>
				<td class="num">{fmtPrice(report.retry.pnl, L)} cr</td>
			</tr>
		</tbody>
	</table>
	<p class="muted small">{t('mistake.sideBySide')}</p>
</section>

<style>
	table {
		border-collapse: collapse;
		margin: 0.5rem 0;
	}
	th,
	td {
		padding: 0.25rem 0.9rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	td.num {
		text-align: right;
	}
</style>
