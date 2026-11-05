// Mission objectives as text (task 4.1). The engine declares each mission's objectives as codes with
// an optional number (MissionRules, `missionRules.objectives` in the state); this catalog only renders
// them, so every client lists the same objectives with the same numbers.

import type { MissionRulesView, ObjectiveCode } from '#lib/engine/types.ts';
import { fmtPct } from './format.ts';
import type { Locale } from './i18n.svelte';

type Catalog = Record<ObjectiveCode, string>;

// `{pct}` is the objective's value (valueBp) as a percentage in the player's locale.
const PT: Catalog = {
	plan_every_entry: 'Registre uma hipótese e um ponto de invalidação antes de cada entrada.',
	risk_per_idea: 'Use stop: risco planejado de até {pct} do patrimônio por ideia.',
	no_liquidation_wait_ok: 'Não seja liquidado. Esperar é uma decisão válida.',
	liq_distance: 'Mantenha a distância até a liquidação acima de {pct}.',
	leverage_from_stop: 'Escolha a alavancagem pelo stop, não pelo máximo permitido.',
	liquidation_loses_margin: 'Liquidação = perder toda a margem isolada.',
	bankruptcy_game_over: 'Se o capital acabar, é game over.',
	option_lose_scenario: 'Antes de comprar, escreva em que cenário o trade perde (movimento, tempo ou volatilidade).',
	premium_vs_breakeven: 'Compare o prêmio com o movimento necessário até o ponto de equilíbrio.',
	watch_iv_event: 'Observe a volatilidade implícita (IV) antes e depois do evento.',
	writer_worst_case: 'Antes de vender, calcule a margem e o pior cenário.',
	maintenance_buffer: 'Mantenha folga sobre a margem de manutenção: abaixo dela há recompra forçada.',
	theta_for_seller: 'Theta trabalha a favor do vendedor — até o mercado se mover.',
	limit_orders_at_zones: 'Entre e saia com ordens limitadas nas zonas e compare a linha de spread e slippage no debrief.',
	unfilled_limit_ok: 'Uma limitada que não executou é um trade perdido, não um prejuízo.',
	risk_from_stop: 'Risco de até {pct} por ideia, com a quantidade calculada pelo stop.',
	trail_not_tighter_than_atr: 'No lucro, leve o stop à entrada ou use stop móvel, mas nunca mais apertado que o ruído (ATR).',
	drawdown_budget: 'Termine sem drawdown acima de {pct}.',
	regime_in_plan: 'Antes de cada entrada, escreva no plano em que regime você acha que está e por quê.',
	one_tool_per_regime: 'Use uma ferramenta por regime.',
	straddle_breakevens: 'Antes de comprar, calcule os dois pontos de equilíbrio do straddle ou do strangle.',
	straddle_lose_scenario: 'Escreva no plano o cenário em que o trade perde (sem surpresa, queda da IV).',
	premium_budget: 'Não pague mais que {pct} do patrimônio em prêmio.',
	structure_max_loss: 'Antes de operar, escreva a perda máxima de cada estrutura (put de proteção, collar, spread).',
	written_margin_buffer: 'Mantenha folga sobre a margem de manutenção das opções vendidas.',
	funding_vs_theta: 'Compare o funding que você paga com o theta que você paga.',
	decide_before_release: 'Antes de cada divulgação, decida ficar fora, pequeno ou protegido, e registre por quê.',
	no_chasing_headline: 'Não opere a manchete depois do gap sem um motivo novo.',
	risk_with_gap: 'Risco planejado de até {pct}, contando o gap.',
	finish_mission: 'Termine a missão.',
	plan_and_stop_every_entry: 'Cada entrada com plano e stop.',
	no_liquidation_no_margin_call: 'Nenhuma liquidação e nenhuma chamada de margem.',
	read_campaign_report: 'Leia o relatório da campanha antes de concluir qualquer coisa.'
};

const EN: Catalog = {
	plan_every_entry: 'Record a hypothesis and an invalidation level before every entry.',
	risk_per_idea: 'Use a stop: planned risk of at most {pct} of equity per idea.',
	no_liquidation_wait_ok: 'Do not get liquidated. Waiting is a valid decision.',
	liq_distance: 'Keep the distance to liquidation above {pct}.',
	leverage_from_stop: 'Choose leverage from your stop, not from the maximum allowed.',
	liquidation_loses_margin: 'Liquidation = losing the whole isolated margin.',
	bankruptcy_game_over: 'If capital runs out, it is game over.',
	option_lose_scenario: 'Before buying, write the scenario in which the trade loses (move, time or volatility).',
	premium_vs_breakeven: 'Compare the premium with the move needed to reach breakeven.',
	watch_iv_event: 'Watch implied volatility (IV) before and after the event.',
	writer_worst_case: 'Before selling, compute the margin and the worst case.',
	maintenance_buffer: 'Keep a buffer above maintenance margin: below it there is a forced buy-back.',
	theta_for_seller: 'Theta works for the seller — until the market moves.',
	limit_orders_at_zones: 'Enter and exit with limit orders at the zones, and compare the spread and slippage line in the debrief.',
	unfilled_limit_ok: 'An unfilled limit is a missed trade, not a loss.',
	risk_from_stop: 'Risk at most {pct} per idea, with the quantity set by the stop.',
	trail_not_tighter_than_atr: 'Once in profit, move the stop to break-even or trail it, but never tighter than the noise (ATR).',
	drawdown_budget: 'Finish without a drawdown above {pct}.',
	regime_in_plan: 'Before each entry, write in the plan which regime you think you are in, and why.',
	one_tool_per_regime: 'Use one tool per regime.',
	straddle_breakevens: 'Before buying, compute the straddle’s or strangle’s two breakevens.',
	straddle_lose_scenario: 'Write in the plan the scenario in which the trade loses (no surprise, falling IV).',
	premium_budget: 'Do not pay more than {pct} of equity in premium.',
	structure_max_loss: 'Before trading, write each structure’s maximum loss (protective put, collar, spread).',
	written_margin_buffer: 'Keep a buffer above the maintenance margin of written options.',
	funding_vs_theta: 'Compare the funding you pay with the theta you pay.',
	decide_before_release: 'Before each release, decide to be flat, small or hedged, and record why.',
	no_chasing_headline: 'Do not trade the headline after the gap without a new reason.',
	risk_with_gap: 'Planned risk of at most {pct}, counting the gap.',
	finish_mission: 'Finish the mission.',
	plan_and_stop_every_entry: 'Every entry with a plan and a stop.',
	no_liquidation_no_margin_call: 'No liquidation and no margin call.',
	read_campaign_report: 'Read the campaign report before concluding anything.'
};

const CATALOGS: Record<Locale, Catalog> = { 'pt-BR': PT, en: EN };

/** Basis points as a percentage in the locale, without trailing zeros (100 -> "1%", 250 -> "2,5%"). */
export function bpPct(bp: number, locale: Locale): string {
	return fmtPct(bp / 10_000, locale, pctDigits(bp));
}

function pctDigits(bp: number): number {
	if (bp % 100 === 0) return 0;
	return bp % 10 === 0 ? 1 : 2;
}

/** One objective as text; an unknown code (a newer engine) renders as its code. */
export function objectiveText(o: { code: ObjectiveCode; valueBp: number }, locale: Locale): string {
	const template = CATALOGS[locale][o.code] ?? o.code;
	return template.replace('{pct}', bpPct(o.valueBp, locale));
}

export function objectiveTexts(rules: MissionRulesView, locale: Locale): string[] {
	return rules.objectives.map((o) => objectiveText(o, locale));
}

export const OBJECTIVE_CODES = Object.keys(PT) as ObjectiveCode[];
