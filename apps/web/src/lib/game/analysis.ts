// Debrief texts for the engine's score report (engine/scoring is authoritative: the client only
// renders codes as words). PlanEntry is the local journal entry; its free text is never scored.

import type { EngineFinding, LessonCode } from '#lib/engine/types.ts';
import { fmtNum, fmtPrice } from './format.ts';
import { bpPct } from './objectives.ts';
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
	loseScenario?: string;
	instrument?: string; // option series label or combo name; absent = perpetual
	combo?: string; // the strategy-builder combo this plan opened (its id)
	/** "Protect with: trailing stop" (add-practice-and-advanced-strategies D10): offered after the fill; client data only. */
	trail?: { mode: 'price' | 'pct'; value: string };
}

const findingText: Record<Locale, Record<EngineFinding['code'], string>> = {
	'pt-BR': {
		waited: 'Você não operou. Sem hipótese clara, esperar é uma decisão válida — não há penalidade por não girar.',
		liquidated:
			'Liquidação na barra {step} a {price}: a margem isolada inteira ({amount}) foi perdida. Alavancagem menor teria colocado o stop antes da liquidação.',
		stop_beyond_liq: 'Houve stop posicionado além do preço de liquidação: a liquidação dispara antes do stop.',
		no_stop: '{count} entradas sem stop de proteção logo em seguida. Sem stop, o risco planejado é indefinido.',
		gap_slippage: 'Stop solicitado em {requested} executou em {price} por causa de um gap: stop a mercado não garante preço.',
		funding_cost: 'Você pagou {amount} cr de funding — mais do que em taxas. Carregar posição lotada custa.',
		ambiguous: '{count} execuções usaram a aproximação intrabar conservadora (OHLC não revela a ordem máxima/mínima).',
		capacity: '{count} execuções foram limitadas pela capacidade simulada da barra.',
		good_process_loss: 'Prejuízo com processo bom: cada trade tinha plano e risco limitado (stop ou prêmio), sem liquidação nem chamada de margem. Isso é gestão de risco funcionando.',
		good_process_win:
			'Lucro com processo bom: cada trade tinha plano e risco limitado (stop ou prêmio), sem liquidação nem chamada de margem. Mantenha o processo — um resultado sozinho não prova vantagem.',
		lucky_win: 'Lucro sem processo completo (sem plano ou sem stop). Resultado favorável pode ter sido sorte.',
		no_plan: 'Entradas sem plano registrado: sem hipótese e invalidação, não dá para aprender com o resultado.',
		oversized: 'Uma entrada arriscava ~{risk} cr (> 2% do capital). Dimensione pela perda máxima: o stop no futuro, o prêmio na opção.',
		bankrupt: 'Game over: o capital não cobre mais a menor ordem possível.',
		margin_called:
			'Chamada de margem na barra {step}: as opções vendidas foram recompradas à força a {price}, perda de {amount} cr. Quem vende opção precisa de margem para o pior movimento: venda menos contratos.',
		theta_decay:
			'O tempo (theta) custou {amount} cr nas opções compradas — pelo menos metade da perda. Quem compra opção paga pelo tempo: o movimento precisa vir antes do vencimento.',
		iv_crush:
			'A queda da volatilidade implícita (vega) custou {amount} cr nas opções compradas — pelo menos metade da perda. Comprar com IV alta é pagar caro pela mesma aposta.',
		iv_rise:
			'A alta da volatilidade implícita (vega) custou {amount} cr nas opções vendidas — pelo menos metade da perda. Quem vende opção fica vendido em volatilidade: medo e notícia sobem a IV e encarecem a recompra, mesmo sem o preço chegar ao strike.',
		wrote_options:
			'Você vendeu opções a descoberto: o risco não fica limitado por um stop nem pelo prêmio, só pela margem. Por isso não há crédito de processo bom, mesmo com plano — dimensione pelo pior movimento.',
		leverage_vs_stop:
			'Na entrada da barra {step}, a {count}x, a liquidação ({requested}) ficava a menos de duas distâncias do stop ({price}) da entrada: um gap do tamanho do stop além dele já liquidaria a posição. Com alavancagem menor o stop teria folga.',
		liq_buffer: 'A liquidação chegou a {pct} do preço, mais perto que a distância de aviso desta missão{warn}. Deixe margem para o ruído e para os gaps.',
		drawdown_budget: 'O drawdown máximo ({amount} cr) passou do orçamento de drawdown da missão ({pct} do capital). Dimensione cada ideia pelo orçamento que sobra.',
		revenge_trade:
			'Vingança: o trade {trade} fechou com perda de {loss} cr na barra {exit}, e na barra {entry} veio uma entrada nova com {ratio}× o tamanho dele. Depois de uma perda, espere o setup se formar de novo e entre com o tamanho normal.',
		fomo_entry:
			'Medo de ficar de fora (FOMO): a entrada da barra {entry} veio depois de {closes} fechamentos seguidos na mesma direção, a {ext} ATR da média de 20 barras, {stopText}. Longe da estrutura, o stop fica longe ou o tamanho fica minúsculo.',
		averaging_down:
			'Preço médio para baixo: na barra {entry} você aumentou uma posição que perdia {loss} cr, e o preço médio foi de {avgBefore} para {avgAfter}; o risco até o stop passou de {riskBefore} para {riskAfter} cr (plano da primeira entrada: {planRisk} cr). Aumentar só é coerente se foi planejado na entrada, com o risco total até a mesma invalidação.',
		risk_limit_exceeded:
			'O limite de risco de {limit} ({limitPct} do patrimônio) foi passado em {count} fechamentos, a partir da barra {step} (pior: {worstPct}). Reduza ou neutralize antes que o limite seja passado.'
	},
	en: {
		waited: 'You did not trade. Without a clear hypothesis, waiting is a valid decision — no penalty for low turnover.',
		liquidated:
			'Liquidated at bar {step} at {price}: the whole isolated margin ({amount}) was lost. Lower leverage would have put the stop before liquidation.',
		stop_beyond_liq: 'A stop sat beyond the liquidation price: liquidation fires before the stop.',
		no_stop: '{count} entries without a protective stop right after. Without a stop, planned risk is undefined.',
		gap_slippage: 'Stop requested at {requested} filled at {price} because of a gap: a stop-market does not guarantee a price.',
		funding_cost: 'You paid {amount} cr in funding — more than in fees. Carrying a crowded position costs.',
		ambiguous: '{count} fills used the conservative intrabar approximation (OHLC does not reveal whether high or low came first).',
		capacity: '{count} fills were limited by the simulated bar capacity.',
		good_process_loss: 'A loss with good process: every trade had a plan and limited risk (stop or premium), no liquidation or margin call. That is risk management working.',
		good_process_win:
			'A profit with good process: every trade had a plan and limited risk (stop or premium), no liquidation or margin call. Keep the process — one result alone does not prove an edge.',
		lucky_win: 'Profit without full process (no plan or no stop). A favorable outcome may have been luck.',
		no_plan: 'Entries without a recorded plan: without hypothesis and invalidation you cannot learn from the outcome.',
		oversized: 'One entry risked ~{risk} cr (> 2% of capital). Size from the maximum loss: the stop on a future, the premium on an option.',
		bankrupt: 'Game over: capital no longer covers the smallest possible order.',
		margin_called:
			'Margin call at bar {step}: the written options were bought back by force at {price}, a loss of {amount} cr. An option writer needs margin for the worst move: write fewer contracts.',
		theta_decay:
			'Time decay (theta) cost {amount} cr on the bought options — at least half of the loss. An option buyer pays for time: the move has to come before expiry.',
		iv_crush:
			'Falling implied volatility (vega) cost {amount} cr on the bought options — at least half of the loss. Buying when IV is high pays a lot for the same bet.',
		iv_rise:
			'Rising implied volatility (vega) cost {amount} cr on the written options — at least half of the loss. An option writer is short volatility: fear and news lift IV and make the buy-back dearer, even before the price reaches the strike.',
		wrote_options:
			'You wrote options uncovered: the risk is limited by neither a stop nor the premium, only by margin. That is why there is no good-process credit, even with a plan — size for the worst move.',
		leverage_vs_stop:
			'At the entry on bar {step}, at {count}x, liquidation ({requested}) sat less than twice the stop’s distance ({price}) from the entry: a gap one stop-distance beyond the stop would already liquidate it. Lower leverage gives the stop room.',
		liq_buffer: 'Liquidation came within {pct} of the price, closer than this mission’s warning distance{warn}. Leave room for noise and gaps.',
		drawdown_budget: 'The maximum drawdown ({amount} cr) went past the mission’s drawdown budget ({pct} of capital). Size each idea from the budget that is left.',
		revenge_trade:
			'Revenge trade: trade {trade} closed at a loss of {loss} cr on bar {exit}, and on bar {entry} a new entry came at {ratio}× its size. After a loss, wait for the setup to form again and enter at normal size.',
		fomo_entry:
			'Fear of missing out (FOMO): the entry on bar {entry} came after {closes} closes in a row the same way, {ext} ATR from the 20-bar average, {stopText}. Far from structure, the stop sits far away or the size gets tiny.',
		averaging_down:
			'Averaging down: on bar {entry} you added to a position losing {loss} cr, and the average entry went from {avgBefore} to {avgAfter}; the risk to the stop went from {riskBefore} to {riskAfter} cr (the first entry’s plan: {planRisk} cr). Adding is consistent only when it was planned at entry, with the total risk to the same invalidation.',
		risk_limit_exceeded:
			'The {limit} risk limit ({limitPct} of equity) was exceeded at {count} closes, from bar {step} (worst: {worstPct}). Reduce or hedge before the limit is crossed.'
	}
};

// Scoring v14: an unplanned revenge entry, an averaging add without a stop, a chase without one.
const variantText: Record<Locale, Partial<Record<string, string>>> = {
	'pt-BR': {
		'revenge_trade.unplanned':
			'Vingança: o trade {trade} fechou com perda de {loss} cr na barra {exit}, e na barra {entry} veio uma entrada nova sem plano registrado ({ratio}× o tamanho dele). Depois de uma perda, espere o setup se formar de novo e entre com o tamanho normal.',
		'averaging_down.nostop':
			'Preço médio para baixo: na barra {entry} você aumentou uma posição que perdia {loss} cr, sem stop, e o preço médio foi de {avgBefore} para {avgAfter}. Aumentar só é coerente se foi planejado na entrada, com o risco total até a mesma invalidação.',
		'stop.none': 'sem stop',
		'stop.far': 'com o stop em {stop}, a mais de 2 ATR'
	},
	en: {
		'revenge_trade.unplanned':
			'Revenge trade: trade {trade} closed at a loss of {loss} cr on bar {exit}, and on bar {entry} a new entry came with no plan recorded ({ratio}× its size). After a loss, wait for the setup to form again and enter at normal size.',
		'averaging_down.nostop':
			'Averaging down: on bar {entry} you added to a position losing {loss} cr, with no stop, and the average entry went from {avgBefore} to {avgAfter}. Adding is consistent only when it was planned at entry, with the total risk to the same invalidation.',
		'stop.none': 'with no stop',
		'stop.far': 'with the stop at {stop}, more than 2 ATR away'
	}
};

const LIMIT_NAMES: Record<Locale, Record<string, string>> = {
	'pt-BR': { delta: 'delta', gamma: 'gama', vega: 'vega', theta: 'theta' },
	en: { delta: 'delta', gamma: 'gamma', vega: 'vega', theta: 'theta' }
};

// With a safe leverage (count > 0) the engine names the leverage that would have kept the stop in front.
const safeLeverageText: Record<Locale, Partial<Record<EngineFinding['code'], string>>> = {
	'pt-BR': {
		liquidated:
			'Liquidação na barra {step} a {price}: a margem isolada inteira ({amount}) foi perdida. O stop estava além da liquidação: com até {count}x de alavancagem ele teria ficado na frente.',
		stop_beyond_liq:
			'O stop em {price} (entrada na barra {step}) ficou além da liquidação em {requested}: a liquidação dispara antes. Com até {count}x de alavancagem o stop teria ficado na frente.'
	},
	en: {
		liquidated:
			'Liquidated at bar {step} at {price}: the whole isolated margin ({amount}) was lost. The stop sat beyond liquidation: up to {count}x leverage would have kept it in front.',
		stop_beyond_liq:
			'The stop at {price} (entry at bar {step}) sat beyond the liquidation price {requested}: liquidation fires first. Up to {count}x leverage would have kept the stop in front.'
	}
};

// Count findings with a count of exactly 1.
const singularText: Record<Locale, Partial<Record<EngineFinding['code'], string>>> = {
	'pt-BR': {
		no_stop: '1 entrada sem stop de proteção logo em seguida. Sem stop, o risco planejado é indefinido.',
		ambiguous: '1 execução usou a aproximação intrabar conservadora (OHLC não revela a ordem máxima/mínima).',
		capacity: '1 execução foi limitada pela capacidade simulada da barra.'
	},
	en: {
		no_stop: '1 entry without a protective stop right after. Without a stop, planned risk is undefined.',
		ambiguous: '1 fill used the conservative intrabar approximation (OHLC does not reveal whether high or low came first).',
		capacity: '1 fill was limited by the simulated bar capacity.'
	}
};

// The liquidation line when the stop_beyond_liq finding already names the stop and the safe leverage.
const liquidatedOnlyText: Record<Locale, string> = {
	'pt-BR': 'Liquidação na barra {step} a {price}: a margem isolada inteira ({amount}) foi perdida.',
	en: 'Liquidated at bar {step} at {price}: the whole isolated margin ({amount}) was lost.'
};

const lessonText: Record<Locale, Record<LessonCode, string>> = {
	'pt-BR': {
		futures2: 'Futuros II — margem, alavancagem e preço de liquidação',
		risk: 'Risco — dimensionar pelo stop e orçamento de risco',
		futures3: 'Futuros III — funding e custo de carregar posição',
		orders: 'Ordens — gatilho não é execução; gaps e slippage',
		structure: 'Estrutura — hipótese e ponto de invalidação',
		evaluation: 'Avaliação — repetir em mercados novos antes de concluir',
		options: 'Opções II — volatilidade implícita e gregas (theta, vega)'
	},
	en: {
		futures2: 'Futures II — margin, leverage and liquidation price',
		risk: 'Risk — sizing from the stop and a risk budget',
		futures3: 'Futures III — funding and the cost of carry',
		orders: 'Orders — trigger is not execution; gaps and slippage',
		structure: 'Structure — hypothesis and invalidation level',
		evaluation: 'Evaluation — repeat on new markets before concluding',
		options: 'Options II — implied volatility and the Greeks (theta, vega)'
	}
};

/** What the mission declares that a finding refers to (engine MissionRules): the liquidation warning distance. */
export interface FindingContext {
	liqWarningBp: number;
}

// The evidence of a scoring v14 finding as text parameters (prices and amounts in the locale).
function evidenceParams(f: EngineFinding, locale: Locale): Record<string, string | number> {
	const e = f.evidence;
	if (!e) return {};
	const money = (v?: string) => (v == null ? '' : fmtPrice(v, locale));
	const stopFar = e.stop != null && Number(e.stop) > 0;
	return {
		trade: e.trade ?? '',
		exit: e.exitStep ?? '',
		entry: e.entryStep ?? e.addStep ?? '',
		loss: money(e.loss ?? e.unrealized),
		ratio: e.sizeRatioBp == null ? '' : fmtNum(e.sizeRatioBp / 10_000, locale, 2),
		ext: e.extensionAtr == null ? '' : fmtNum(e.extensionAtr / 100, locale, 2),
		closes: e.closes ?? '',
		stopText: fill(variantText[locale][stopFar ? 'stop.far' : 'stop.none'] ?? '', { stop: money(e.stop) }),
		avgBefore: money(e.avgBefore),
		avgAfter: money(e.avgAfter),
		riskBefore: money(e.riskBefore),
		riskAfter: money(e.riskAfter),
		planRisk: money(e.planRisk),
		limit: e.limit ? LIMIT_NAMES[locale][e.limit] : '',
		limitPct: e.limitBp == null ? '' : bpPct(e.limitBp, locale),
		worstPct: e.worstBp == null ? '' : bpPct(e.worstBp, locale)
	};
}

function variantOf(f: EngineFinding, locale: Locale): string | undefined {
	const e = f.evidence;
	if (f.code === 'revenge_trade' && e && !e.planned) return variantText[locale]['revenge_trade.unplanned'];
	if (f.code === 'averaging_down' && e && !(Number(e.stop ?? '0') > 0)) return variantText[locale]['averaging_down.nostop'];
	return undefined;
}

export function describeFinding(f: EngineFinding, locale: Locale, ctx?: FindingContext): string {
	const params: Record<string, string | number> = {
		step: f.step,
		count: f.count,
		price: fmtPrice(f.price, locale),
		requested: fmtPrice(f.requested, locale),
		amount: fmtPrice(f.amount, locale),
		risk: Math.round(Number(f.amount)).toLocaleString(locale),
		pct: bpPct(f.count, locale),
		warn: ctx ? ` (${bpPct(ctx.liqWarningBp, locale)})` : '',
		...evidenceParams(f, locale)
	};
	const special = f.count > 0 ? safeLeverageText[locale][f.code] : undefined;
	const template = special ?? variantOf(f, locale) ?? (f.count === 1 ? singularText[locale][f.code] : undefined) ?? findingText[locale][f.code];
	return fill(template, params);
}

function fill(template: string, params: Record<string, string | number>): string {
	return template.replace(/\{(\w+)\}/g, (_, k: string) => String(params[k] ?? ''));
}

/**
 * Texts for a whole findings list. The safe-leverage advice is said once: when the stop_beyond_liq
 * finding carries it, the liquidation line only states the loss.
 */
export function describeFindings(findings: EngineFinding[], locale: Locale, ctx?: FindingContext): string[] {
	const stopNamed = findings.some((f) => f.code === 'stop_beyond_liq' && f.count > 0);
	return findings.map((f) =>
		stopNamed && f.code === 'liquidated'
			? fill(liquidatedOnlyText[locale], { step: f.step, price: fmtPrice(f.price, locale), amount: fmtPrice(f.amount, locale) })
			: describeFinding(f, locale, ctx)
	);
}

export function describeLesson(l: LessonCode, locale: Locale): string {
	return lessonText[locale][l];
}
