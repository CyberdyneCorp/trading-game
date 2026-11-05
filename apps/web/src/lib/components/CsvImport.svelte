<script lang="ts">
	import { untrack } from 'svelte';
	import type { GameSession } from '#lib/game/session.svelte.ts';
	import { CsvImportWizard } from '#lib/game/csvImport.svelte.ts';
	import { csvIssueText, t, tCount } from '#lib/game/i18n.svelte.ts';
	import { GAP_FILLS, INTERVALS, isoUtc } from '#lib/game/packs.ts';

	let { session }: { session: GameSession } = $props();
	// One wizard per visit of the import screen (the component is recreated each time).
	const w = untrack(() => new CsvImportWizard(session));
	const r = $derived(w.report);

	function onFile(e: Event & { currentTarget: HTMLInputElement }) {
		const file = e.currentTarget.files?.[0];
		if (file) void w.chooseFile(file);
	}
</script>

<main class="import">
	<div class="panel">
		<h1>{t('csv.title')}</h1>
		<p>{t('csv.intro')}</p>
		<p class="muted small">{t('csv.privacy')}</p>

		<label class="field">
			{t('csv.file')}
			<input type="file" accept=".csv,text/csv" onchange={onFile} data-testid="csv-file" />
		</label>
		{#if w.fileName}<p class="muted small">{w.fileName}</p>{/if}

		{#if w.step !== 'file'}
			<fieldset class="meta" disabled={w.busy || w.step === 'report'}>
				<label>{t('csv.asset')} <input bind:value={w.meta.asset} maxlength="64" data-testid="csv-asset" /></label>
				<label>{t('csv.pair')} <input bind:value={w.meta.pair} maxlength="64" data-testid="csv-pair" /></label>
				<label>{t('csv.venue')} <input bind:value={w.meta.venue} maxlength="64" /></label>
				<label>
					{t('csv.interval')}
					<select bind:value={w.meta.intervalSeconds} data-testid="csv-interval">
						{#each INTERVALS as i (i.seconds)}<option value={i.seconds}>{i.label}</option>{/each}
					</select>
				</label>
				<label>
					{t('csv.tsUnit')}
					<select bind:value={w.meta.timestampUnit} data-testid="csv-unit">
						<option value="s">{t('csv.seconds')}</option>
						<option value="ms">{t('csv.millis')}</option>
					</select>
				</label>
				<label>
					{t('csv.volumeUnit')}
					<select bind:value={w.meta.volumeUnit}>
						<option value="base">{t('csv.base')}</option>
						<option value="quote">{t('csv.quote')}</option>
					</select>
				</label>
				<label>{t('csv.leverage')} <input type="number" min="1" max="20" bind:value={w.meta.leverageCap} /></label>
				<label>{t('csv.label')} <input bind:value={w.label} placeholder={w.suggestedLabel} maxlength="60" data-testid="csv-label" /></label>
			</fieldset>
		{/if}

		{#if r}
			<section class="report" data-testid="csv-report" aria-live="polite">
				{#if r.ok}
					<p class="up">✔ {t('csv.valid')}</p>
					<p>
						{t('csv.rows', { n: r.rows })} · {t('csv.period', { from: isoUtc(r.firstTime), to: isoUtc(r.lastTime) })} ·
						{r.hasFunding ? t('csv.withFunding') : t('csv.noFunding')}
					</p>
				{:else}
					<p class="down">✖ {tCount('csv.errors', r.errorCount)}</p>
					<ul class="issues">
						{#each r.errors as e, i (i)}
							<li data-testid="csv-error">{#if e.line}<span class="muted">{t('csv.line', { line: e.line })}:</span>{/if} {csvIssueText(e.code)}</li>
						{/each}
						{#if r.errorCount > r.errors.length}<li class="muted">{t('csv.more', { n: r.errorCount - r.errors.length })}</li>{/if}
					</ul>
				{/if}
				{#if r.gapCount > 0}
					<p class="attention">⚠ {tCount('csv.gaps', r.gapCount)}</p>
					<ul class="issues">
						{#each r.gaps as g, i (i)}<li>{tCount('csv.gapLine', g.missing, { line: g.line })}</li>{/each}
					</ul>
					{#if r.ok}
						<label class="gap-fill">
							{t('csv.gapFill')}
							<select bind:value={w.meta.gapFill} data-testid="csv-gap-fill">
								{#each GAP_FILLS as g (g.value)}<option value={g.value}>{t(g.label)}</option>{/each}
							</select>
						</label>
						<p class="muted small">{t('csv.gapFillHint')}</p>
					{/if}
				{/if}
			</section>
		{/if}

		{#if w.error}<p class="down" role="alert" data-testid="csv-alert">{w.error}</p>{/if}
		{#if w.busy}<p class="muted">{t('csv.working')}</p>{/if}

		<div class="actions">
			<span class="left">
				<button onclick={() => w.cancel()}>{t('csv.cancel')}</button>
				{#if w.step === 'report'}<button onclick={() => w.back()}>{t('csv.back')}</button>{/if}
			</span>
			{#if w.step === 'meta'}
				<button class="primary" onclick={() => w.validate()} disabled={w.busy} data-testid="csv-validate">{t('csv.validate')}</button>
			{:else if w.step === 'report' && r?.ok}
				<button class="primary" onclick={() => w.create()} disabled={w.busy} data-testid="csv-create">▶ {t('csv.create')}</button>
			{/if}
		</div>
	</div>
</main>

<style>
	.import {
		max-width: 760px;
		margin: 3rem auto;
		padding: 0 1rem;
	}
	.field,
	.meta label {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
	}
	.meta {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
		gap: 0.6rem;
		border: none;
		padding: 0;
		margin: 1rem 0;
	}
	.report {
		border-left: 3px solid var(--info);
		padding-left: 0.7rem;
		margin: 1rem 0;
	}
	.issues {
		max-height: 14rem;
		overflow: auto;
		font-size: 0.85rem;
	}
	.actions {
		display: flex;
		justify-content: space-between;
		margin-top: 1rem;
	}
	.left {
		display: flex;
		gap: 0.4rem;
	}
	.small {
		font-size: 0.8rem;
	}
</style>
