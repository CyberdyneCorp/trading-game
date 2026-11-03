// Debrief analysis: separates process from outcome (spec gameplay-loop "Debrief", mission-scoring).
// Preview implementation for the Phase 1 slice; the authoritative scorer moves to engine/scoring.

import type { DebriefData, EngineEvent, GameState } from '#lib/engine/types.ts';
import type { Locale } from './i18n.svelte';

export interface PlanEntry {
	step: number;
	side: 'buy' | 'sell';
	thesis: string;
	invalidation: number | null;
	target: number | null;
	confidence: number;
	plannedRisk: number | null;
	skipped: boolean;
}

export type FindingCode =
	| 'waited'
	| 'liquidated'
	| 'stop_beyond_liq'
	| 'no_stop'
	| 'gap_slippage'
	| 'funding_cost'
	| 'ambiguous'
	| 'capacity'
	| 'good_process_loss'
	| 'lucky_win'
	| 'no_plan'
	| 'oversized'
	| 'bankrupt';

export interface Finding {
	code: FindingCode;
	tone: 'good' | 'warn' | 'bad' | 'info';
	params: Record<string, string | number>;
}

export type LessonCode = 'futures2' | 'risk' | 'futures3' | 'orders' | 'structure' | 'evaluation';

export interface ScoreDim {
	weight: number;
	score: number | null; // null = not applicable (renormalized)
}

export interface Analysis {
	netPnl: number;
	findings: Finding[];
	lesson: LessonCode;
	dims: { risk: ScoreDim; plan: ScoreDim; understanding: ScoreDim; calibration: ScoreDim };
	total: number | null;
	entries: number;
}

interface Facts {
	entries: EngineEvent[];
	liquidations: EngineEvent[];
	stopFills: EngineEvent[];
	gapStops: EngineEvent[];
	ambiguous: number;
	capacity: number;
	unprotectedEntries: number;
	stopBeyondLiq: boolean;
}

const n = (d: string) => Number(d);

function collectFacts(state: GameState, stopWarnings: number): Facts {
	const fills = state.events.filter((e) => e.type === 'fill');
	const entries = fills.filter((e) => n(e.pnl) === 0 && !isExit(state, e));
	const stopOrderIds = new Set(
		state.events.filter((e) => e.type === 'order' && e.status === 'accepted' && n(e.requested) > 0).map((e) => e.orderId)
	);
	const stopFills = fills.filter((e) => isExit(state, e) && stopOrderIds.has(e.orderId) && n(e.pnl) < 0);
	return {
		entries,
		liquidations: state.events.filter((e) => e.type === 'liquidation'),
		stopFills,
		gapStops: stopFills.filter((e) => e.gap),
		ambiguous: fills.filter((e) => e.ambiguous).length,
		capacity: fills.filter((e) => e.capacityLimited).length,
		unprotectedEntries: countUnprotected(state, entries),
		stopBeyondLiq: stopWarnings > 0
	};
}

function isExit(state: GameState, fill: EngineEvent): boolean {
	return n(fill.pnl) !== 0 || state.events.some((e) => e.type === 'order' && e.orderId === fill.orderId && e.reason === 'position_closed');
}

// An entry is protected if a reduce-only stop order was accepted within the next two steps.
function countUnprotected(state: GameState, entries: EngineEvent[]): number {
	const stops = state.events.filter((e) => e.type === 'order' && e.status === 'accepted' && n(e.requested) > 0);
	return entries.filter((f) => !stops.some((s) => s.step >= f.step - 1 && s.step <= f.step + 2 && s.side !== f.side)).length;
}

function riskScore(state: GameState, facts: Facts, journal: PlanEntry[]): number {
	if (facts.entries.length === 0 && facts.liquidations.length === 0) return 100;
	let s = 100;
	if (facts.liquidations.length > 0) s -= 40;
	s -= Math.min(40, facts.unprotectedEntries * 20);
	const equity0 = n(state.initialBalance);
	if (journal.some((p) => p.plannedRisk != null && p.plannedRisk > 0.02 * equity0)) s -= 20;
	if (facts.stopBeyondLiq) s -= 15;
	if (n(state.totals.maxDrawdown) > 0.2 * equity0) s -= 15;
	return Math.max(0, s);
}

function planScore(facts: Facts, journal: PlanEntry[]): number | null {
	if (facts.entries.length === 0) return null;
	const planned = journal.filter((p) => !p.skipped).length;
	const ratio = Math.min(1, planned / facts.entries.length);
	const withInvalidation = journal.filter((p) => !p.skipped && p.invalidation != null).length;
	const invRatio = planned === 0 ? 0 : withInvalidation / planned;
	return Math.round(70 * ratio + 30 * invRatio);
}

function combine(dims: Analysis['dims']): number | null {
	const parts = Object.values(dims).filter((d) => d.score != null);
	const w = parts.reduce((a, d) => a + d.weight, 0);
	if (w === 0) return null;
	return Math.round(parts.reduce((a, d) => a + d.weight * (d.score as number), 0) / w);
}

function outcomeFindings(net: number, facts: Facts, journal: PlanEntry[], out: Finding[]): void {
	const planned = journal.some((p) => !p.skipped);
	if (facts.entries.length === 0) out.push({ code: 'waited', tone: 'good', params: {} });
	if (net < 0 && planned && facts.unprotectedEntries === 0 && facts.liquidations.length === 0) {
		out.push({ code: 'good_process_loss', tone: 'good', params: {} });
	}
	if (net > 0 && (!planned || facts.unprotectedEntries > 0)) out.push({ code: 'lucky_win', tone: 'warn', params: {} });
	if (facts.entries.length > 0 && !planned) out.push({ code: 'no_plan', tone: 'warn', params: {} });
}

function riskFindings(state: GameState, facts: Facts, journal: PlanEntry[], out: Finding[]): void {
	if (state.status === 'bankrupt') out.push({ code: 'bankrupt', tone: 'bad', params: {} });
	for (const l of facts.liquidations) {
		out.push({ code: 'liquidated', tone: 'bad', params: { step: l.step, price: l.price, amount: l.amount } });
	}
	if (facts.stopBeyondLiq) out.push({ code: 'stop_beyond_liq', tone: 'bad', params: {} });
	if (facts.unprotectedEntries > 0) out.push({ code: 'no_stop', tone: 'warn', params: { count: facts.unprotectedEntries } });
	const big = journal.find((p) => p.plannedRisk != null && p.plannedRisk > 0.02 * n(state.initialBalance));
	if (big) out.push({ code: 'oversized', tone: 'warn', params: { risk: Math.round(big.plannedRisk ?? 0) } });
}

function executionFindings(state: GameState, facts: Facts, out: Finding[]): void {
	for (const g of facts.gapStops) {
		out.push({ code: 'gap_slippage', tone: 'info', params: { requested: g.requested, price: g.price } });
	}
	const funding = n(state.totals.fundingNet);
	if (funding < 0 && Math.abs(funding) > n(state.totals.feesPaid)) {
		out.push({ code: 'funding_cost', tone: 'warn', params: { amount: Math.abs(funding).toFixed(2) } });
	}
	if (facts.ambiguous > 0) out.push({ code: 'ambiguous', tone: 'info', params: { count: facts.ambiguous } });
	if (facts.capacity > 0) out.push({ code: 'capacity', tone: 'info', params: { count: facts.capacity } });
}

function pickLesson(codes: Set<FindingCode>): LessonCode {
	if (codes.has('liquidated') || codes.has('stop_beyond_liq') || codes.has('bankrupt')) return 'futures2';
	if (codes.has('no_stop') || codes.has('oversized')) return 'risk';
	if (codes.has('funding_cost')) return 'futures3';
	if (codes.has('gap_slippage')) return 'orders';
	if (codes.has('no_plan')) return 'structure';
	return 'evaluation';
}

export function analyze(state: GameState, debrief: DebriefData, journal: PlanEntry[], stopWarnings: number): Analysis {
	const facts = collectFacts(state, stopWarnings);
	const netPnl = n(debrief.finalEquity) - n(state.initialBalance);
	const findings: Finding[] = [];
	riskFindings(state, facts, journal, findings);
	outcomeFindings(netPnl, facts, journal, findings);
	executionFindings(state, facts, findings);
	const dims = {
		risk: { weight: 35, score: riskScore(state, facts, journal) },
		plan: { weight: 25, score: planScore(facts, journal) },
		understanding: { weight: 25, score: null },
		calibration: { weight: 15, score: null }
	};
	return {
		netPnl,
		findings,
		lesson: pickLesson(new Set(findings.map((f) => f.code))),
		dims,
		total: combine(dims),
		entries: facts.entries.length
	};
}

const findingText: Record<Locale, Record<FindingCode, string>> = {
	'pt-BR': {
		waited: 'Você não operou. Sem hipótese clara, esperar é uma decisão válida — não há penalidade por não girar.',
		liquidated:
			'Liquidação na barra {step} a {price}: a margem isolada inteira ({amount}) foi perdida. Alavancagem menor teria colocado o stop antes da liquidação.',
		stop_beyond_liq: 'Houve stop posicionado além do preço de liquidação: a liquidação dispara antes do stop.',
		no_stop: '{count} entrada(s) sem stop de proteção logo em seguida. Sem stop, o risco planejado é indefinido.',
		gap_slippage: 'Stop solicitado em {requested} executou em {price} por causa de um gap: stop a mercado não garante preço.',
		funding_cost: 'Você pagou {amount} cr de funding — mais do que em taxas. Carregar posição lotada custa.',
		ambiguous: '{count} execução(ões) usaram a aproximação intrabar conservadora (OHLC não revela a ordem máxima/mínima).',
		capacity: '{count} execução(ões) foram limitadas pela capacidade simulada da barra.',
		good_process_loss: 'Prejuízo com processo bom: plano registrado, stop respeitado, sem liquidação. Isso é gestão de risco funcionando.',
		lucky_win: 'Lucro sem processo completo (sem plano ou sem stop). Resultado favorável pode ter sido sorte.',
		no_plan: 'Entradas sem plano registrado: sem hipótese e invalidação, não dá para aprender com o resultado.',
		oversized: 'Um plano arriscava ~{risk} cr (> 2% do capital). Dimensione pelo stop.',
		bankrupt: 'Game over: o capital não cobre mais a menor ordem possível.'
	},
	en: {
		waited: 'You did not trade. Without a clear hypothesis, waiting is a valid decision — no penalty for low turnover.',
		liquidated:
			'Liquidated at bar {step} at {price}: the whole isolated margin ({amount}) was lost. Lower leverage would have put the stop before liquidation.',
		stop_beyond_liq: 'A stop sat beyond the liquidation price: liquidation fires before the stop.',
		no_stop: '{count} entry(ies) without a protective stop right after. Without a stop, planned risk is undefined.',
		gap_slippage: 'Stop requested at {requested} filled at {price} because of a gap: a stop-market does not guarantee a price.',
		funding_cost: 'You paid {amount} cr in funding — more than in fees. Carrying a crowded position costs.',
		ambiguous: '{count} fill(s) used the conservative intrabar approximation (OHLC does not reveal whether high or low came first).',
		capacity: '{count} fill(s) were limited by the simulated bar capacity.',
		good_process_loss: 'A loss with good process: plan recorded, stop respected, no liquidation. That is risk management working.',
		lucky_win: 'Profit without full process (no plan or no stop). A favorable outcome may have been luck.',
		no_plan: 'Entries without a recorded plan: without hypothesis and invalidation you cannot learn from the outcome.',
		oversized: 'One plan risked ~{risk} cr (> 2% of capital). Size from the stop.',
		bankrupt: 'Game over: capital no longer covers the smallest possible order.'
	}
};

const lessonText: Record<Locale, Record<LessonCode, string>> = {
	'pt-BR': {
		futures2: 'Futuros II — margem, alavancagem e preço de liquidação',
		risk: 'Risco — dimensionar pelo stop e orçamento de risco',
		futures3: 'Futuros III — funding e custo de carregar posição',
		orders: 'Ordens — gatilho não é execução; gaps e slippage',
		structure: 'Estrutura — hipótese e ponto de invalidação',
		evaluation: 'Avaliação — repetir em mercados novos antes de concluir'
	},
	en: {
		futures2: 'Futures II — margin, leverage and liquidation price',
		risk: 'Risk — sizing from the stop and a risk budget',
		futures3: 'Futures III — funding and the cost of carry',
		orders: 'Orders — trigger is not execution; gaps and slippage',
		structure: 'Structure — hypothesis and invalidation level',
		evaluation: 'Evaluation — repeat on new markets before concluding'
	}
};

export function describeFinding(f: Finding, locale: Locale): string {
	return findingText[locale][f.code].replace(/\{(\w+)\}/g, (_, k: string) => String(f.params[k] ?? ''));
}

export function describeLesson(l: LessonCode, locale: Locale): string {
	return lessonText[locale][l];
}
