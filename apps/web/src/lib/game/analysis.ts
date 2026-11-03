// Debrief texts for the engine's score report (engine/scoring is authoritative: the client only
// renders codes as words). PlanEntry is the local journal entry; its free text is never scored.

import type { EngineFinding, LessonCode } from '#lib/engine/types.ts';
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

export function describeFinding(f: EngineFinding, locale: Locale): string {
	const params: Record<string, string | number> = {
		step: f.step,
		count: f.count,
		price: Number(f.price).toFixed(2),
		requested: Number(f.requested).toFixed(2),
		amount: Number(f.amount).toFixed(2),
		risk: Math.round(Number(f.amount))
	};
	return findingText[locale][f.code].replace(/\{(\w+)\}/g, (_, k: string) => String(params[k] ?? ''));
}

export function describeLesson(l: LessonCode, locale: Locale): string {
	return lessonText[locale][l];
}
