// Player-facing mission texts, keyed by engine mission id (engine/src/missions.cpp).

import type { Locale } from './i18n.svelte';

export interface MissionText {
	title: string;
	tagline: string;
	objectives: string[];
	lesson: string;
}

const texts: Record<string, Record<Locale, MissionText>> = {
	'm01-false-breakout': {
		'pt-BR': {
			title: '01 · Rompimento falso',
			tagline: 'Um range longo, uma tentativa de rompimento… e o mercado pode discordar de você.',
			objectives: [
				'Registre uma hipótese e um ponto de invalidação antes de cada entrada.',
				'Use stop: risco planejado de até 1–2% do patrimônio por ideia.',
				'Não seja liquidado. Esperar confirmação é uma decisão válida.'
			],
			lesson: 'Futuros I · long e short, stop não garante preço'
		},
		en: {
			title: '01 · False breakout',
			tagline: 'A long range, a breakout attempt… and the market may disagree with you.',
			objectives: [
				'Record a hypothesis and an invalidation level before every entry.',
				'Use a stop: planned risk of at most 1–2% of equity per idea.',
				'Do not get liquidated. Waiting for confirmation is a valid decision.'
			],
			lesson: 'Futures I · long and short, a stop does not guarantee a price'
		}
	},
	'm02-funding-squeeze': {
		'pt-BR': {
			title: '02 · Aperto de funding',
			tagline: 'Tendência forte de alta — mas segurar um long custa caro quando todos estão comprados.',
			objectives: [
				'Observe a taxa de funding publicada antes de cada intervalo.',
				'Compare o funding pago com o ganho do movimento.',
				'Mantenha a distância até a liquidação acima de 10%.'
			],
			lesson: 'Futuros III · funding, carry e posicionamento lotado'
		},
		en: {
			title: '02 · Funding squeeze',
			tagline: 'Strong uptrend — but holding a long gets expensive when everyone is long.',
			objectives: [
				'Watch the published funding rate before each interval.',
				'Compare the funding you pay with the move you capture.',
				'Keep the distance to liquidation above 10%.'
			],
			lesson: 'Futures III · funding, carry and crowded positioning'
		}
	},
	'm03-volatility-shock': {
		'pt-BR': {
			title: '03 · Choque de volatilidade',
			tagline: 'Mercado calmo convida a alavancar. Até que deixa de ser calmo.',
			objectives: [
				'Escolha a alavancagem pelo stop, não pelo máximo permitido.',
				'Sobreviva ao choque: liquidação = perder toda a margem isolada.',
				'Se o capital acabar, é game over.'
			],
			lesson: 'Futuros II · margem, alavancagem e preço de liquidação'
		},
		en: {
			title: '03 · Volatility shock',
			tagline: 'A calm market invites leverage. Until it stops being calm.',
			objectives: [
				'Choose leverage from your stop, not from the maximum allowed.',
				'Survive the shock: liquidation = losing the whole isolated margin.',
				'If capital runs out, it is game over.'
			],
			lesson: 'Futures II · margin, leverage and liquidation price'
		}
	}
};

export function missionText(id: string, locale: Locale): MissionText {
	return texts[id]?.[locale] ?? { title: id, tagline: '', objectives: [], lesson: '' };
}
