// Web parity fixtures of add-practice-and-advanced-strategies wave 2 (scoring v14 debrief views): the
// web's objectiveChecks.ts, analysis.ts describeFindings and EpisodesPanel.svelte's formatting, run
// over real engine debriefs of the m18-m25 goldens (tests/fixtures/engine/debriefs-w2.json, replayed by
// the desktop's engine in tests/viewmodel/test_debrief_v14.cpp) and over variants of their findings
// that reach every wording branch.

import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const INPUTS = resolve(dirname(fileURLToPath(import.meta.url)), '../tests/fixtures/engine/debriefs-w2.json');
const LOCALES = ['pt-BR', 'en'];

function cases() {
	const list = [];
	const add = (fn, args, out) => list.push({ fn, args, out: out === undefined ? null : out });
	return { list, add };
}

// EpisodesPanel.svelte's cells, written with the web's own format.ts helpers as the component does.
function episodeCells(f, t, e, L) {
	const r = (bp) => (bp == null ? '—' : `${f.fmtNum(bp / 10_000, L, 2, true)}R`);
	const fills = (xs) => xs.map((x) => `${f.fmtQty(x.qty, L)} @ ${f.fmtPrice(x.price, L)} (${x.step})`).join(' · ');
	const moves = e.stopMoves.map((m) => `${t(`episodes.move.${m.kind}`, L)} ${f.fmtPrice(m.price, L)} (${m.step})`);
	if (e.trailing) moves.push(`[${t('episodes.trailing', L)}]`);
	return [
		String(e.n),
		e.side === 'long' ? '▲ LONG' : '▼ SHORT',
		fills(e.entries) + (e.entries.length > 1 ? ` [${t('episodes.scaled', L)}]` : ''),
		e.exits.length ? fills(e.exits) : t('episodes.open', L),
		moves.length ? moves.join(' · ') : '—',
		`${r(e.rBp)} · ${f.fmtMoney(e.net, L, true)}`,
		`${r(e.mfeRBp)} · ${r(e.maeRBp == null ? null : 0 - e.maeRBp)}`
	];
}

// Each bias finding's other wording: a planned revenge entry, an add or a chase without a stop.
function variants(findings) {
	const out = [];
	for (const f of findings) {
		if (!f.evidence) continue;
		if (f.code === 'revenge_trade') out.push({ ...f, evidence: { ...f.evidence, planned: true } });
		if (f.code === 'averaging_down' || f.code === 'fomo_entry') out.push({ ...f, evidence: { ...f.evidence, stop: null } });
	}
	return out;
}

// A synthetic episode set for the shapes the goldens lack: scaled in, a partial exit, a trailing stop,
// every stop move and an episode still open at the end.
const SYNTHETIC = [
	{ n: 1, side: 'long', entryStep: 20, exitStep: 41, entries: [{ step: 20, qty: '10', price: '100.5' }, { step: 24, qty: '5', price: '101.25' }], exits: [{ step: 30, qty: '7.5', price: '103' }, { step: 41, qty: '7.5', price: '102.1' }], stopMoves: [{ step: 22, kind: 'to_breakeven', price: '100.5' }, { step: 26, kind: 'toward', price: '101' }], trailing: true, net: '41.37', plannedRisk: '50', rBp: 8274, mfeRBp: 12000, maeRBp: 3000 },
	{ n: 2, side: 'short', entryStep: 50, exitStep: null, entries: [{ step: 50, qty: '3', price: '99' }], exits: [], stopMoves: [{ step: 52, kind: 'away', price: '101.5' }], trailing: false, net: '-12.5', plannedRisk: '0', rBp: null, mfeRBp: null, maeRBp: null }
];

export function debriefV14Fixture(checks, analysis, format, i18n) {
	const { list, add } = cases();
	const t = (key, L) => {
		i18n.i18n.locale = L;
		return i18n.t(key);
	};
	const inputs = JSON.parse(readFileSync(INPUTS, 'utf8'));
	for (const d of inputs) {
		add('objectiveReadings', [d, Number(d.initialBalance)], checks.objectiveReadings(d, d.initialBalance));
		const findings = [...d.findings, ...variants(d.findings)];
		for (const f of findings) add('findingMoment', [f], checks.findingMoment(f));
		const ctx = d.missionRules.liqWarningBp >= 0 ? { liqWarningBp: d.missionRules.liqWarningBp } : undefined;
		for (const L of LOCALES) {
			add('describeFindings', [findings, L, d.missionRules.liqWarningBp], analysis.describeFindings(findings, L, ctx));
			for (const e of d.episodes) add('episodeCells', [e, L], episodeCells(format, t, e, L));
		}
	}
	for (const L of LOCALES) for (const e of SYNTHETIC) add('episodeCells', [e, L], episodeCells(format, t, e, L));
	// The m22 readings on the synthetic episodes: a trailing stop, a partial exit and a stop moved away.
	const m22 = { missionRules: { objectives: [{ code: 'total_risk_all_entries', valueBp: 100 }, { code: 'partial_exit_or_trail', valueBp: 0 }, { code: 'never_stop_away', valueBp: 0 }] }, findings: [], episodes: SYNTHETIC };
	add('objectiveReadings', [m22, 10000], checks.objectiveReadings(m22, '10000'));
	add('objectiveReadings', [{ ...m22, episodes: [] }, 10000], checks.objectiveReadings({ ...m22, episodes: [] }, '10000'));
	// Two stops filling on the closing bar are one exit, not a partial one (W2-X2).
	const together = [{ ...SYNTHETIC[0], trailing: false, exits: [{ step: 41, qty: '10', price: '99.02' }, { step: 41, qty: '5', price: '99.01' }] }];
	add('objectiveReadings', [{ ...m22, episodes: together }, 10000], checks.objectiveReadings({ ...m22, episodes: together }, '10000'));
	i18n.i18n.locale = 'pt-BR';
	return { helper: 'objectiveChecks.ts + analysis.ts + EpisodesPanel.svelte', cases: list };
}
