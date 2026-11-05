import { describe, expect, it } from 'vitest';
import type { CsvBuildResult } from '#lib/engine/protocol.ts';
import type { CsvMeta, CsvReport } from '#lib/engine/types.ts';
import { CsvImportWizard } from './csvImport.svelte.ts';
import { i18n } from './i18n.svelte.ts';
import type { GameSession } from './session.svelte.ts';

const OK: CsvReport = {
	ok: true, rows: 120, errors: [], errorCount: 0, gaps: [], gapCount: 0, hasFunding: false, firstTime: 0, lastTime: 0
};

function fakeSession(report: CsvReport, build: Partial<CsvBuildResult> = {}) {
	const calls: { meta?: CsvMeta; label?: string; menu: number } = { menu: 0 };
	const session = {
		validateCsv: async (_csv: string, meta: CsvMeta) => ((calls.meta = meta), report),
		importCsv: async (_csv: string, _meta: CsvMeta, label: string) => (
			(calls.label = label), { ok: true, reason: '', report, scenarioId: 'csv-1', digest: 'd', size: 1, pack: new Uint8Array(), ...build }
		),
		backToMenu: () => void calls.menu++
	} as unknown as GameSession;
	return { session, calls };
}

const file = (text: string, name = 'eth.csv') => new File([text], name, { type: 'text/csv' });
const CSV = 'time,open,high,low,close,volume\n1700000000000,1,1,1,1,1\n1700000900000,1,1,1,1,1\n';

describe('CsvImportWizard', () => {
	it('file -> guessed metadata -> report -> pack named after the pair', async () => {
		const { session, calls } = fakeSession(OK);
		const w = new CsvImportWizard(session);
		await w.chooseFile(file(CSV));
		expect(w.step).toBe('meta');
		expect(w.meta).toMatchObject({ timestampUnit: 'ms', intervalSeconds: 900 });
		w.meta.pair = 'ETH/USDT';
		await w.validate();
		expect(w.step).toBe('report');
		expect(calls.meta?.pair).toBe('ETH/USDT');
		await w.create();
		expect(calls.label).toBe('ETH/USDT · 15m');
		expect(calls.menu).toBe(1);
	});

	it('stays on the metadata step when the engine rejects a declared value', async () => {
		const { session } = fakeSession({ ...OK, ok: false, metaError: 'intervalSeconds' });
		const w = new CsvImportWizard(session);
		await w.chooseFile(file(CSV));
		i18n.locale = 'en';
		await w.validate();
		expect(w.step).toBe('meta');
		expect(w.error).toBe('Bar interval: from 1 minute to 7 days.');
		i18n.locale = 'pt-BR';
	});

	it('regression: a refused leverage names the field in words and gives the allowed range', async () => {
		const { session } = fakeSession({ ...OK, ok: false, metaError: 'leverageCap' });
		const w = new CsvImportWizard(session);
		await w.chooseFile(file(CSV));
		await w.validate();
		expect(w.error).toBe('Alavancagem máxima: use um número inteiro de 1 a 20.');
		expect(w.error).not.toContain('leverageCap');
	});

	it('regression: a build refused as csv_invalid is translated, not shown as the raw code', async () => {
		const { session } = fakeSession(OK, { ok: false, reason: 'csv_invalid' });
		const w = new CsvImportWizard(session);
		await w.chooseFile(file(CSV));
		await w.validate();
		await w.create();
		expect(w.error).toBe('Não foi possível criar a missão: o arquivo não passou na validação do motor');
	});

	it('explains a refused pack and keeps the wizard open', async () => {
		const { session, calls } = fakeSession(OK, { ok: false, reason: 'untrusted_key' });
		const w = new CsvImportWizard(session);
		await w.chooseFile(file(CSV));
		await w.validate();
		i18n.locale = 'en';
		await w.create();
		expect(w.error).toMatch(/Could not create the mission: signed by an unknown key/);
		expect(calls.menu).toBe(0);
		i18n.locale = 'pt-BR';
	});

	it('refuses files above the engine limit before reading them', async () => {
		const { session } = fakeSession(OK);
		const w = new CsvImportWizard(session);
		i18n.locale = 'en';
		await w.chooseFile(file('x'.repeat(4 * 1024 * 1024 + 1)));
		expect(w.step).toBe('file');
		expect(w.error).toBe('File too large (max 4 MB).');
		i18n.locale = 'pt-BR';
	});
});
