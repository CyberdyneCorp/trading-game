<script lang="ts">
	// Advisor control (spec advisor "Advice in the terminal and the debrief"): shows the points left,
	// asks the engine for a read of the visible market and shows the latest one with its reasons.
	// "Apply" only fills the ticket; it never places an order.
	import type { AdviceView } from '#lib/engine/types.ts';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { pctFromBp, reasonLine, voteMark } from '#lib/game/advisor.ts';
	import { fmtPrice, fmtQty, num } from '#lib/game/format.ts';
	import { i18n, reasonText, t } from '#lib/game/i18n.svelte.ts';

	let { session, onApply }: { session: GameSession; onApply: (a: AdviceView) => void } = $props();

	const s = $derived(session.state!);
	const adv = $derived(s.advisor);
	const latest = $derived(adv.advice.at(-1) ?? null);
	const askedNow = $derived(latest?.step === s.step);
	const canAsk = $derived(adv.left > 0 && !askedNow && !session.busy && s.status === 'active');
	const actionable = $derived(latest != null && latest.stance !== 'wait' && num(latest.size) > 0 && latest.step === s.step);
	const L = $derived(i18n.locale);

	async function ask() {
		const res = await session.requestAdvice();
		if (res && !res.accepted) session.notice = { tone: 'error', text: `✖ ${reasonText(res.reason)}` };
	}

	function apply() {
		if (!latest || !actionable) return;
		onApply(latest);
		session.notice = { tone: 'ok', text: t('adv.applied') };
	}
</script>

<section class="panel advisor" aria-label={t('adv.title')}>
	<h2 class="panel-title">{t('adv.title')} <span class="tag" data-testid="advisor-points">{t('adv.left', { n: adv.left })}</span></h2>
	<button class="ask" disabled={!canAsk} onclick={ask} title={askedNow ? t('adv.askedNow') : undefined}>{t('adv.ask')}</button>
	{#if adv.left === 0 && !latest}
		<p class="muted small">{t('adv.empty')}</p>
	{:else if !latest}
		<p class="muted small">{t('adv.intro')}</p>
	{/if}
	{#if latest}
		<div class="read" data-testid="advice">
			<div class="stance {latest.stance === 'long' ? 'up' : latest.stance === 'short' ? 'down' : 'attention'}">
				{t(`adv.stance.${latest.stance}`)}
				<span class="small">{t('adv.confidence', { pct: pctFromBp(latest.confidenceBp) })} · {t('adv.at', { n: latest.step })}</span>
			</div>
			{#if latest.stance !== 'wait'}
				<dl class="kv">
					<dt>{t('adv.stop')}</dt><dd>{fmtPrice(latest.stop, L)}</dd>
					<dt>{t('adv.size')}</dt><dd>{num(latest.size) > 0 ? fmtQty(latest.size, L) : '—'}</dd>
				</dl>
			{/if}
			<ul class="reasons">
				{#each latest.reasons as r, i (i)}
					<li><span class={r.vote > 0 ? 'up' : r.vote < 0 ? 'down' : 'muted'}>{voteMark(r.vote)}</span> {reasonLine(r, s.indicators.patterns, L)}</li>
				{/each}
			</ul>
			{#if actionable}
				<button onclick={apply}>{t('adv.apply')}</button>
			{/if}
			<p class="muted small">{t('adv.disclaimer')}</p>
		</div>
	{/if}
</section>

<style>
	.advisor {
		overflow: auto;
		flex-shrink: 0;
		max-height: 45%;
	}
	.ask {
		width: 100%;
		margin-bottom: 0.3rem;
	}
	.stance {
		font-family: var(--font-pixel);
		font-size: 1.3rem;
	}
	.stance .small {
		font-family: inherit;
		font-size: 0.75rem;
		margin-left: 0.4rem;
	}
	.reasons {
		list-style: none;
		margin: 0.3rem 0;
		padding: 0;
		font-size: 0.8rem;
	}
	.small {
		font-size: 0.75rem;
		margin: 0.2rem 0 0;
	}
	.tag {
		margin-left: 0.3rem;
	}
</style>
