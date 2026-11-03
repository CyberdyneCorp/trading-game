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
				'Este cenário foi desenhado como um ROMPIMENTO FALSO: o preço rompe o topo do range, atrai compradores e reverte com força. Quem entrou no rompimento sem stop sofreu; quem esperou confirmação ou protegeu a posição limitou o dano. O rumor da alta já era esperado (sem surpresa) e não mudou o valor justo; o desmentido, crível e negativo, mudou. Dentro do range, comprar perto do suporte e vender perto da resistência teve vantagem pequena.'
		},
		en: {
			title: 'Mission 01 · Range',
			tagline: 'The market has been moving sideways for weeks. What comes next is up to you.',
			objectives: COMMON_EN,
			topic: 'Futures I · long and short, orders and stops',
			reveal:
				'This scenario was designed as a FALSE BREAKOUT: price breaks the top of the range, attracts buyers and reverses hard. Buying the breakout without a stop hurt; waiting for confirmation or protecting the position limited the damage. The rally rumor was already expected (no surprise) and did not move fair value; the credible negative denial did. Inside the range, buying near support and selling near resistance had a small edge.'
		}
	},
	'm02-funding-squeeze': {
		'pt-BR': {
			title: 'Missão 02 · Momentum',
			tagline: 'O preço sobe com consistência. Observe também o que custa ficar posicionado.',
			objectives: [...COMMON_PT.slice(0, 2), 'Mantenha a distância até a liquidação acima de 10%.'],
			topic: 'Futuros III · funding e custo de carregar posição',
			reveal:
				'Este cenário foi desenhado como um APERTO DE FUNDING: a alta veio com longs lotados pagando funding alto, seguida de uma correção. Carregar long por muito tempo custou caro mesmo quando o preço subiu. Na tendência, os recuos até a média (EMA) foram compras melhores que os topos, e funding extremo tornou provável um aperto contra os longs lotados.'
		},
		en: {
			title: 'Mission 02 · Momentum',
			tagline: 'Price keeps grinding higher. Also watch what it costs to stay positioned.',
			objectives: [...COMMON_EN.slice(0, 2), 'Keep the distance to liquidation above 10%.'],
			topic: 'Futures III · funding and the cost of carry',
			reveal:
				'This scenario was designed as a FUNDING SQUEEZE: the rally came with crowded longs paying high funding, followed by a correction. Carrying a long for long cost a lot even while price rose. In the trend, pullbacks to the average (EMA) were better buys than the highs, and extreme funding made a squeeze against crowded longs likely.'
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
				'Este cenário foi desenhado como um CHOQUE DE VOLATILIDADE: depois de um longo período calmo, gaps e quedas bruscas atravessam stops e liquidam posições muito alavancadas. Antes do choque a volatilidade foi se comprimindo (Bandas de Bollinger e ATR estreitando): mercado quieto não é mercado seguro.'
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
				'This scenario was designed as a VOLATILITY SHOCK: after a long calm period, gaps and sharp drops cut through stops and liquidate highly leveraged positions. Before the shock, volatility kept compressing (Bollinger bands and ATR narrowing): a quiet market is not a safe market.'
		}
	}
};

texts['m04-options-buyer'] = {
	'pt-BR': {
		title: 'Missão 04 · Calls e puts',
		tagline: 'Pela primeira vez você pode comprar opções. O prêmio é o seu risco máximo — escolha bem o que paga.',
		objectives: [
			'Antes de comprar, escreva em que cenário o trade perde (movimento, tempo ou volatilidade).',
			'Compare o prêmio com o movimento necessário até o ponto de equilíbrio.',
			'Observe a volatilidade implícita (IV) antes e depois do evento.'
		],
		topic: 'Opções I e II · payoff, prêmio, IV e gregas',
		reveal:
			'Este cenário foi desenhado com um EVENTO: a IV sobe antes do rompimento e despenca depois (IV crush). Quem comprou opções caras perto do evento pôde perder mesmo acertando a direção; quem comprou cedo ou com strike adequado aproveitou o movimento. A direção do rompimento não era garantida: a surpresa positiva da decisão inclinava as chances para cima, mas em parte das sementes o rompimento foi para baixo.'
	},
	en: {
		title: 'Mission 04 · Calls and puts',
		tagline: 'For the first time you can buy options. The premium is your maximum risk — choose carefully what you pay.',
		objectives: [
			'Before buying, write the scenario in which the trade loses (move, time or volatility).',
			'Compare the premium with the move needed to reach breakeven.',
			'Watch implied volatility (IV) before and after the event.'
		],
		topic: 'Options I and II · payoff, premium, IV and Greeks',
		reveal:
			'This scenario was designed around an EVENT: IV rises before the breakout and collapses afterwards (IV crush). Buying expensive options near the event could lose even with the right direction; buying early or with a sensible strike captured the move. The breakout direction was not guaranteed: the decision’s positive surprise tilted the odds upward, but on some seeds the breakout went down.'
	}
};

texts['m05-options-writer'] = {
	'pt-BR': {
		title: 'Missão 05 · Vendendo prêmio',
		tagline: 'Agora você pode vender opções e receber o prêmio. Receber pouco para arriscar muito exige margem.',
		objectives: [
			'Antes de vender, calcule a margem e o pior cenário.',
			'Mantenha folga sobre a margem de manutenção: abaixo dela há recompra forçada.',
			'Theta trabalha a favor do vendedor — até o mercado se mover.'
		],
		topic: 'Opções III · venda de opções, margem e chamada de margem',
		reveal:
			'Este cenário foi desenhado com prêmio farto em um mercado lateral seguido de um SALTO. Vender opções rendeu theta por muitas barras; sem tamanho controlado, o salto gerou chamada de margem e recompra no pior momento.'
	},
	en: {
		title: 'Mission 05 · Selling premium',
		tagline: 'Now you can sell options and collect the premium. Collecting little to risk a lot needs margin.',
		objectives: [
			'Before selling, compute the margin and the worst case.',
			'Keep a buffer above maintenance margin: below it there is a forced buy-back.',
			'Theta works for the seller — until the market moves.'
		],
		topic: 'Options III · writing options, margin and margin calls',
		reveal:
			'This scenario was designed with rich premium in a sideways market followed by a JUMP. Selling options earned theta for many bars; without controlled size, the jump triggered a margin call and a buy-back at the worst moment.'
	}
};

export function missionText(id: string, locale: Locale): MissionText {
	return texts[id]?.[locale] ?? { title: id, tagline: '', objectives: [], topic: '', reveal: '' };
}
