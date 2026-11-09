// Run files (add-run-sharing-and-journal-stats D1–D3, D9): the web's draft of a file from a run
// record, the shared fixtures through the browser's own WASM build, and the exporter writing the
// fixtures' canonical files from the same runs (so a desktop-exported file and a web-exported one
// are the same bytes).

import { beforeAll, describe, expect, it } from 'vitest';
import type { RunFileVerdict } from '#lib/engine/types.ts';
import { loadEngine, wasmBuilt, type NodeEngine } from '#lib/engine/wasmNode.test-util.ts';
import type { PlanEntry } from './analysis.ts';
import { decimalText, exportBlock, noteFromFile, noteToFile, refusalKey, runFileDraft, runFileName, utf8Length } from './runFile.ts';
import type { RunRecord } from './storage.ts';

// node:fs without @types/node (the web app does not depend on it); tests run in Node.
const NODE_FS = 'node:fs';
const { readFileSync } = (await import(/* @vite-ignore */ NODE_FS)) as { readFileSync(path: URL): Uint8Array };
const repo = new URL('../../../../../', import.meta.url);
const read = (path: string) => new TextDecoder().decode(readFileSync(new URL(path, repo)));

interface IndexEntry {
	name: string;
	expected: Record<string, unknown> & { ok: boolean };
}
const index = JSON.parse(read('engine/tests/runfiles/index.json')) as { files: IndexEntry[] };

const STAMP = { executionPolicy: 18, abi: 1, version: '0.1.0' };

function record(p: Partial<RunRecord<PlanEntry, unknown>> = {}): RunRecord<PlanEntry, unknown> {
	return {
		runId: 'run-x-123',
		missionId: 'm01-false-breakout',
		seed: 7,
		createdAt: 1_700_000_000_000,
		executionPolicy: 18,
		journal: [],
		questions: [],
		stopWarnings: 0,
		digest: 'd',
		status: 'finished',
		actions: 2,
		...p
	};
}

const plan: PlanEntry = { step: 70, side: 'buy', thesis: 'range low', invalidation: 97.5, target: 104, confidence: 60, plannedRisk: 100.004, skipped: false };

describe('run file draft (D1, D3)', () => {
	it('writes amounts as decimal text with at most 8 decimals, and nothing for a non-number', () => {
		expect(decimalText(97.5)).toBe('97.5');
		expect(decimalText(100)).toBe('100');
		expect(decimalText(0.123456789)).toBe('0.12345679');
		expect(decimalText(NaN)).toBeNull();
		expect(decimalText(-1)).toBeNull();
		expect(decimalText(null)).toBeNull();
	});

	it('holds no run id, timestamps or device identity, and leaves the notes out unless asked', () => {
		const r = record({ journal: [plan], options: { adviceAllowance: 3, newsLead: 1, calendar: true }, campaign: { id: 'camp-1', index: 2 } });
		const draft = runFileDraft(r, [{ k: 'adv', n: 3 }], STAMP, false);
		const text = JSON.stringify(draft);
		expect(text).not.toContain('run-x-123');
		expect(text).not.toContain('1700000000000');
		expect(text).not.toContain('camp-1');
		expect(draft.journal).toEqual([]);
		expect(draft.labels).toEqual({ campaign: true, assisted: false });
		expect(draft.options).toEqual({ adviceAllowance: 3, newsLead: 1, calendar: true });
		expect(Object.keys(draft)).toEqual(['format', 'version', 'mission', 'pack', 'seed', 'options', 'engine', 'labels', 'actions', 'journal']);
	});

	it('includes notes on request, round-tripping their numbers', () => {
		const draft = runFileDraft(record({ journal: [plan] }), [], STAMP, true);
		expect(draft.journal[0]).toEqual({ step: 70, side: 'buy', thesis: 'range low', invalidation: '97.5', target: '104', plannedRisk: '100.004', confidence: 60, skipped: false });
		expect(noteFromFile(noteToFile(plan))).toEqual(plan);
	});

	it('runs on private packs and runs without actions are not exportable', () => {
		const packs = [
			{ digest: 'a', private: true, origin: 'import' as const },
			{ digest: 'b', private: false, origin: 'catalog' as const }
		];
		expect(exportBlock(record({ packDigest: 'a' }), packs)).toBe('private_pack');
		expect(exportBlock(record({ packDigest: 'b' }), packs)).toBeNull();
		expect(exportBlock(record({ packDigest: 'c' }), packs)).toBe('pack_missing');
		expect(exportBlock(record({ actions: 0 }), packs)).toBe('no_actions');
		expect(exportBlock(record(), packs)).toBeNull();
	});

	it('names the file by mission and seed, and explains refusals with their details', () => {
		expect(runFileName('m01-false-breakout', 42)).toBe('tradelab-m01-false-breakout-42.tlrun');
		expect(refusalKey({ reason: 'policy_mismatch', detail: '16' }, 17)).toEqual({ key: 'runfile.reason.policy_mismatch', params: { file: '16', engine: 17 } });
		expect(refusalKey({ reason: 'invalid_field', path: 'actions[3].qty' }, 17).params).toEqual({ path: 'actions[3].qty' });
		expect(refusalKey({ reason: 'digest_mismatch' }, 17).key).toBe('runfile.reason.digest_mismatch');
		expect(utf8Length('ação')).toBe(6);
	});
});

let engine: NodeEngine;
const verify = (text: string) => engine.call<RunFileVerdict>('tl_run_file_verify', [text, 0, 0, '[]'], ['string', 'number', 'number', 'string']);
const seal = (text: string) => engine.call<RunFileVerdict>('tl_run_file_seal', [text, 0, 0, '[]'], ['string', 'number', 'number', 'string']);

describe.skipIf(!wasmBuilt)('shared run-file fixtures through the browser engine (D9)', () => {
	beforeAll(async () => {
		engine = await loadEngine();
	});

	it.each(index.files.map((f) => [f.name, f] as const))('%s gets its expected verdict', (_, entry) => {
		const verdict = verify(read(`engine/tests/runfiles/${entry.name}`)) as unknown as Record<string, unknown>;
		for (const [key, value] of Object.entries(entry.expected)) expect(verdict[key], key).toEqual(value);
	});

	it('the exporter writes the fixtures: a golden run drafted by the web and sealed equals the shared file', () => {
		for (const [golden, fixture] of [
			['m01-false-breakout', 'm01-finished.tlrun'],
			['m13-call-spread', 'm13-call-spread.tlrun'],
			['m08-regime-switch', 'm08-rejections.tlrun']
		]) {
			const fx = JSON.parse(read(`engine/tests/golden/${golden}.json`)) as { mission: string; seed: number; actions: unknown[]; options?: RunRecord['options'] };
			const r = record({ missionId: fx.mission, seed: fx.seed, options: fx.options, actions: fx.actions.length });
			const sealed = seal(JSON.stringify(runFileDraft(r, fx.actions, STAMP, false)));
			const expected = index.files.find((f) => f.name === fixture)!.expected;
			expect(sealed.ok).toBe(true);
			expect(sealed.fileDigest).toBe(expected.fileDigest);
		}
	});
});
