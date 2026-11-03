// Player-facing mission texts, keyed by engine mission id (engine/src/missions.cpp).
// Titles and taglines are deliberately neutral: they must not reveal what the market will do.
// What the scenario was designed to teach is revealed only in the debrief (`reveal`).

import type { Locale } from './i18n.svelte';

export interface MissionText {
	title: string;
	tagline: string;
	objectives: string[];
	topic: string;
	reveal: string;
}

const COMMON_PT = [
	'Registre uma hipótese e um ponto de invalidação antes de cada entrada.',
	'Use stop: risco planejado de até 1–2% do patrimônio por ideia.',
	'Não seja liquidado. Esperar é uma decisão válida.'
];
const COMMON_EN = [
	'Record a hypothesis and an invalidation level before every entry.',
	'Use a stop: planned risk of at most 1–2% of equity per idea.',
	'Do not get liquidated. Waiting is a valid decision.'
];

const texts: Record<string, Record<Locale, MissionText>> = {
	'm01-false-breakout': {
		'pt-BR': {
			title: 'Missão 01 · Range',
			tagline: 'O mercado passou semanas andando de lado. O que vem a seguir é com você.',
			objectives: COMMON_PT,
			topic: 'Futuros I · long e short, ordens e stops',
			reveal:
				'Este cenário foi desenhado como um ROMPIMENTO FALSO: o preço rompe o topo do range, atrai compradores e reverte com força. Quem entrou no rompimento sem stop sofreu; quem esperou confirmação ou protegeu a posição limitou o dano.'
		},
		en: {
			title: 'Mission 01 · Range',
			tagline: 'The market has been moving sideways for weeks. What comes next is up to you.',
			objectives: COMMON_EN,
			topic: 'Futures I · long and short, orders and stops',
			reveal:
				'This scenario was designed as a FALSE BREAKOUT: price breaks the top of the range, attracts buyers and reverses hard. Buying the breakout without a stop hurt; waiting for confirmation or protecting the position limited the damage.'
		}
	},
	'm02-funding-squeeze': {
		'pt-BR': {
			title: 'Missão 02 · Momentum',
			tagline: 'O preço sobe com consistência. Observe também o que custa ficar posicionado.',
			objectives: [...COMMON_PT.slice(0, 2), 'Mantenha a distância até a liquidação acima de 10%.'],
			topic: 'Futuros III · funding e custo de carregar posição',
			reveal:
				'Este cenário foi desenhado como um APERTO DE FUNDING: a alta veio com longs lotados pagando funding alto, seguida de uma correção. Carregar long por muito tempo custou caro mesmo quando o preço subiu.'
		},
		en: {
			title: 'Mission 02 · Momentum',
			tagline: 'Price keeps grinding higher. Also watch what it costs to stay positioned.',
			objectives: [...COMMON_EN.slice(0, 2), 'Keep the distance to liquidation above 10%.'],
			topic: 'Futures III · funding and the cost of carry',
			reveal:
				'This scenario was designed as a FUNDING SQUEEZE: the rally came with crowded longs paying high funding, followed by a correction. Carrying a long for long cost a lot even while price rose.'
		}
	},
	'm03-volatility-shock': {
		'pt-BR': {
			title: 'Missão 03 · Mercado quieto',
			tagline: 'Volatilidade baixa, movimentos pequenos. Escolha sua alavancagem com cuidado.',
			objectives: [
				'Escolha a alavancagem pelo stop, não pelo máximo permitido.',
				'Liquidação = perder toda a margem isolada.',
				'Se o capital acabar, é game over.'
			],
			topic: 'Futuros II · margem, alavancagem e preço de liquidação',
			reveal:
				'Este cenário foi desenhado como um CHOQUE DE VOLATILIDADE: depois de um longo período calmo, gaps e quedas bruscas atravessam stops e liquidam posições muito alavancadas.'
		},
		en: {
			title: 'Mission 03 · Quiet market',
			tagline: 'Low volatility, small moves. Choose your leverage carefully.',
			objectives: [
				'Choose leverage from your stop, not from the maximum allowed.',
				'Liquidation = losing the whole isolated margin.',
				'If capital runs out, it is game over.'
			],
			topic: 'Futures II · margin, leverage and liquidation price',
			reveal:
				'This scenario was designed as a VOLATILITY SHOCK: after a long calm period, gaps and sharp drops cut through stops and liquidate highly leveraged positions.'
		}
	}
};

export function missionText(id: string, locale: Locale): MissionText {
	return texts[id]?.[locale] ?? { title: id, tagline: '', objectives: [], topic: '', reveal: '' };
}
