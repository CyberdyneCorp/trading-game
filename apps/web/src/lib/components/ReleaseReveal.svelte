<script lang="ts">
	// Debrief only (design D8.2 m09/m11, G2/G4): each release this run reached, with its consensus,
	// the drawn outcome and size, the gap it opened, the sampled reaction, and whether the player was
	// positioned through it or entered after it. For option missions, the move the ATM straddle
	// implied the bar before against the move that followed.
	import type { EngineTrade, RevealEvent } from '#lib/engine/types.ts';
	import { fmtBp, releaseRows } from '#lib/game/debrief.ts';
	import { fmtNum } from '#lib/game/format.ts';
	import { i18n, t, tCount } from '#lib/game/i18n.svelte.ts';
	import { kindName, level } from '#lib/game/news.ts';

	let { events, trades, lastStep }: { events: RevealEvent[]; trades: EngineTrade[]; lastStep: number } = $props();
	const L = $derived(i18n.locale);
	const rows = $derived(releaseRows(events, trades, lastStep));
	const implied = $derived(rows.filter((r) => r.impliedBp >= 0));
	const reaction = (r: number) => t(r > 0 ? 'release.reaction.up' : r < 0 ? 'release.reaction.down' : 'release.reaction.none');
</script>

{#if rows.length}
	<section class="panel" data-testid="debrief-releases">
		<h2 class="panel-title">{t('debrief.releasesTitle')}</h2>
		<table class="releases">
			<thead>
				<tr>
					<th class="num">{t('release.col.bar')}</th><th>{t('release.col.event')}</th><th>{t('release.col.consensus')}</th>
					<th>{t('release.col.actual')}</th><th class="num">{t('release.col.weighted')}</th><th class="num">{t('release.col.size')}</th>
					<th class="num">{t('release.col.gap')}</th><th>{t('release.col.reaction')}</th><th>{t('release.col.you')}</th>
				</tr>
			</thead>
			<tbody>
				{#each rows as r (r.step + r.kind)}
					<tr data-testid="release-row">
						<td class="num">{r.step}</td>
						<td>
							{kindName(r.kind, L)}
							<span class="muted small">· {r.announceLead > 0 ? tCount('release.announced', r.announceLead) : t('release.unannounced')}</span>
						</td>
						<td>{level(r.expectation, L)}</td>
						<td class={r.surprise > 0 ? 'up' : r.surprise < 0 ? 'down' : ''}>{level(r.actual, L)}</td>
						<td class="num">{fmtNum(r.weighted, L, 2, true)}</td>
						<td class="num">{r.intensityPct}%</td>
						<td class="num {r.gapBp > 0 ? 'up' : r.gapBp < 0 ? 'down' : ''}">{fmtBp(r.gapBp, L)}</td>
						<td>{reaction(r.reaction)}</td>
						<td data-testid="release-you">{t(`release.you.${r.you}`)}</td>
					</tr>
				{/each}
			</tbody>
		</table>
		{#each implied as r (r.step)}
			<p data-testid="release-implied">
				{t('release.implied', { step: r.step, implied: fmtBp(r.impliedBp, L).replace('+', ''), realized: fmtBp(r.moveBp, L) })}
			</p>
		{/each}
		<p class="muted small">{t('debrief.releasesNote')}</p>
	</section>
{/if}

<style>
	.releases {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.85rem;
	}
	.releases th,
	.releases td {
		padding: 0.2rem 0.4rem;
		border-bottom: 1px solid var(--line);
		text-align: left;
	}
	.releases .num {
		text-align: right;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
