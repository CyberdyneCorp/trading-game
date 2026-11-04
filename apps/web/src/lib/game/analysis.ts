// Debrief texts for the engine's score report (engine/scoring is authoritative: the client only
// renders codes as words). PlanEntry is the local journal entry; its free text is never scored.

import type { EngineFinding, LessonCode } from '#lib/engine/types.ts';
import { fmtPrice } from './format.ts';
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
	instrument?: string; // option series label; absent = perpetual
}

const findingText: Record<Locale, Record<EngineFinding['code'], string>> = {
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
		good_process_loss: 'Prejuízo com processo bom: cada trade tinha plano e risco limitado (stop ou prêmio), sem liquidação nem chamada de margem. Isso é gestão de risco funcionando.',
		lucky_win: 'Lucro sem processo completo (sem plano ou sem stop). Resultado favorável pode ter sido sorte.',
		no_plan: 'Entradas sem plano registrado: sem hipótese e invalidação, não dá para aprender com o resultado.',
		oversized: 'Uma entrada arriscava ~{risk} cr (> 2% do capital). Dimensione pela perda máxima: o stop no futuro, o prêmio na opção.',
		bankrupt: 'Game over: o capital não cobre mais a menor ordem possível.',
		margin_called:
			'Chamada de margem na barra {step}: as opções vendidas foram recompradas à força a {price}, perda de {amount} cr. Quem vende opção precisa de margem para o pior movimento: venda menos contratos.',
		theta_decay:
			'O tempo (theta) custou {amount} cr nas opções — pelo menos metade da perda. Quem compra opção paga pelo tempo: o movimento precisa vir antes do vencimento.',
		iv_crush:
			'A queda da volatilidade implícita (vega) custou {amount} cr nas opções — pelo menos metade da perda. Comprar com IV alta é pagar caro pela mesma aposta.'
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
		good_process_loss: 'A loss with good process: every trade had a plan and limited risk (stop or premium), no liquidation or margin call. That is risk management working.',
		lucky_win: 'Profit without full process (no plan or no stop). A favorable outcome may have been luck.',
		no_plan: 'Entries without a recorded plan: without hypothesis and invalidation you cannot learn from the outcome.',
		oversized: 'One entry risked ~{risk} cr (> 2% of capital). Size from the maximum loss: the stop on a future, the premium on an option.',
		bankrupt: 'Game over: capital no longer covers the smallest possible order.',
		margin_called:
			'Margin call at bar {step}: the written options were bought back by force at {price}, a loss of {amount} cr. An option writer needs margin for the worst move: write fewer contracts.',
		theta_decay:
			'Time decay (theta) cost {amount} cr on the options — at least half of the loss. An option buyer pays for time: the move has to come before expiry.',
		iv_crush:
			'Falling implied volatility (vega) cost {amount} cr on the options — at least half of the loss. Buying when IV is high pays a lot for the same bet.'
	}
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

export function describeFinding(f: EngineFinding, locale: Locale): string {
	const params: Record<string, string | number> = {
		step: f.step,
		count: f.count,
		price: fmtPrice(f.price, locale),
		requested: fmtPrice(f.requested, locale),
		amount: fmtPrice(f.amount, locale),
		risk: Math.round(Number(f.amount)).toLocaleString(locale)
	};
	const template = (f.count > 0 ? safeLeverageText[locale][f.code] : undefined) ?? findingText[locale][f.code];
	return template.replace(/\{(\w+)\}/g, (_, k: string) => String(params[k] ?? ''));
}

export function describeLesson(l: LessonCode, locale: Locale): string {
	return lessonText[locale][l];
}
