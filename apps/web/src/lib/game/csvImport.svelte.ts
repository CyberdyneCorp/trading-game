// CSV import wizard view-model (web-client task 4.5): file -> declared metadata -> engine report
// -> private practice pack. Every rule is checked by the engine; the wizard only guesses defaults.

import type { CsvMeta, CsvReport } from '#lib/engine/types.ts';
import { csvMetaText, packReasonText, t } from './i18n.svelte.ts';
import { CSV_MAX_BYTES, DEFAULT_META, defaultLabel, guessMeta } from './packs.ts';
import type { GameSession } from './session.svelte.ts';

export type ImportStep = 'file' | 'meta' | 'report';

export class CsvImportWizard {
	step = $state<ImportStep>('file');
	fileName = $state('');
	meta = $state<CsvMeta>({ ...DEFAULT_META });
	/** The player's name for the pack; empty = `defaultLabel` (pair or asset, and interval). */
	label = $state('');
	report = $state<CsvReport | null>(null);
	error = $state<string | null>(null);
	busy = $state(false);
	private csv = '';

	constructor(private session: GameSession) {}

	async chooseFile(file: File): Promise<void> {
		this.error = null;
		if (file.size > CSV_MAX_BYTES) {
			this.error = t('csv.tooBig');
			return;
		}
		this.csv = await file.text();
		this.fileName = file.name;
		this.meta = guessMeta(this.csv, this.meta);
		this.report = null;
		this.step = 'meta';
	}

	async validate(): Promise<void> {
		await this.run(async () => {
			const report = await this.session.validateCsv(this.csv, this.meta);
			if (report.metaError) {
				this.error = csvMetaText(report.metaError);
				return;
			}
			this.report = report;
			this.step = 'report';
		});
	}

	back(): void {
		this.error = null;
		this.report = null;
		this.step = 'meta';
	}

	get suggestedLabel(): string {
		return defaultLabel(this.meta, this.fileName);
	}

	async create(): Promise<void> {
		await this.run(async () => {
			const label = this.label.trim() || this.suggestedLabel;
			const res = await this.session.importCsv(this.csv, this.meta, label);
			if (res.ok) this.session.backToMenu();
			else this.error = t('csv.failed', { reason: packReasonText(res.reason) });
		});
	}

	cancel(): void {
		this.session.backToMenu();
	}

	private async run(fn: () => Promise<void>): Promise<void> {
		if (this.busy) return;
		this.busy = true;
		this.error = null;
		try {
			await fn();
		} catch (e) {
			this.error = e instanceof Error ? e.message : String(e);
		} finally {
			this.busy = false;
		}
	}
}
