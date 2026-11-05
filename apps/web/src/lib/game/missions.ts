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
				'Este cenário foi desenhado como um ROMPIMENTO FALSO: o preço rompe o topo do range, atrai compradores, perde força e volta. Quem entrou no rompimento sem stop sofreu; quem esperou confirmação ou protegeu a posição limitou o dano. O rumor da alta já era esperado (sem surpresa) e não mudou o valor justo; o desmentido, crível e negativo, mudou — e foi precificado de uma vez, com gap na abertura seguinte: vender depois da manchete já era tarde. Dentro do range, comprar perto do suporte e vender perto da resistência teve vantagem pequena.'
		},
		en: {
			title: 'Mission 01 · Range',
			tagline: 'The market has been moving sideways for weeks. What comes next is up to you.',
			objectives: COMMON_EN,
			topic: 'Futures I · long and short, orders and stops',
			reveal:
				'This scenario was designed as a FALSE BREAKOUT: price breaks the top of the range, attracts buyers, stalls and falls back. Buying the breakout without a stop hurt; waiting for confirmation or protecting the position limited the damage. The rally rumor was already expected (no surprise) and did not move fair value; the credible negative denial did — and it was priced at once, with a gap at the next open: selling after the headline was already late. Inside the range, buying near support and selling near resistance had a small edge.'
		}
	},
	'm02-funding-squeeze': {
		'pt-BR': {
			title: 'Missão 02 · Momentum',
			tagline: 'O preço sobe com consistência. Observe também o que custa ficar posicionado.',
			objectives: [...COMMON_PT.slice(0, 2), 'Mantenha a distância até a liquidação acima de 10%.'],
			topic: 'Futuros III · funding e custo de carregar posição',
			reveal:
				'Este cenário foi desenhado como um APERTO DE FUNDING: a alta veio com longs lotados pagando funding alto, seguida de uma correção. Carregar long por muito tempo custou caro mesmo quando o preço subiu. Na tendência, os recuos até a média (EMA) foram compras melhores que os topos, e funding extremo tornou possível um aperto contra os longs lotados — mas, até ele vir, a multidão costumava estar certa.'
		},
		en: {
			title: 'Mission 02 · Momentum',
			tagline: 'Price keeps grinding higher. Also watch what it costs to stay positioned.',
			objectives: [...COMMON_EN.slice(0, 2), 'Keep the distance to liquidation above 10%.'],
			topic: 'Futures III · funding and the cost of carry',
			reveal:
				'This scenario was designed as a FUNDING SQUEEZE: the rally came with crowded longs paying high funding, followed by a correction. Carrying a long for long cost a lot even while price rose. In the trend, pullbacks to the average (EMA) were better buys than the highs, and extreme funding made a squeeze against crowded longs possible — but until it came, the crowd was usually right.'
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
				'Este cenário foi desenhado como um CHOQUE DE VOLATILIDADE: depois de um longo período calmo, gaps e quedas bruscas atravessam stops e liquidam posições muito alavancadas. Antes do choque a volatilidade foi se comprimindo (Bandas de Bollinger e ATR estreitando): mercado quieto não é mercado seguro. A manchete saiu e a barra seguinte abriu com gap: stops e liquidações executaram no gap, não no preço marcado. Quando o choque chega muda a cada partida.'
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
				'This scenario was designed as a VOLATILITY SHOCK: after a long calm period, gaps and sharp drops cut through stops and liquidate highly leveraged positions. Before the shock, volatility kept compressing (Bollinger bands and ATR narrowing): a quiet market is not a safe market. The headline came out and the next bar opened with a gap: stops and liquidations filled at the gap, not at their price. When the shock comes changes from run to run.'
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
			'Este cenário foi desenhado com prêmio farto em um mercado lateral seguido de um SALTO. Vender opções rendeu theta por muitas barras; sem tamanho controlado, o salto — um gap logo após a manchete da falha, em uma barra diferente a cada partida — gerou chamada de margem e recompra no pior momento.'
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
			'This scenario was designed with rich premium in a sideways market followed by a JUMP. Selling options earned theta for many bars; without controlled size, the jump — a gap right after the outage headline, on a different bar each run — triggered a margin call and a buy-back at the worst moment.'
	}
};

// Phase-2 batch A (design D8.2). m06 opens the campaign (Structure and Orders), m07 assesses Risk
// sizing, m08 Patterns & Indicators. The reveal also points at the regime band and signals table
// that the debrief draws from the engine's seeded reveal.
texts['m06-thin-book'] = {
	'pt-BR': {
		title: 'Missão 06 · Livro fino',
		tagline: 'Um mercado quieto, com poucas ofertas no livro. Cada entrada e cada saída têm um custo.',
		objectives: [
			'Entre e saia com ordens limitadas nas zonas e compare a linha de spread e slippage no debrief.',
			'Risco planejado de até 1% do patrimônio por ideia.',
			'Uma limitada que não executou é um trade perdido, não um prejuízo.'
		],
		topic: 'Estrutura e Ordens · zonas, limite × mercado, spread e capacidade',
		reveal:
			'Este cenário foi desenhado como um LIVRO FINO: suporte e resistência eram zonas, e cada ordem a mercado pagava spread e slippage (cerca de 0,6% ida e volta, um quarto do range). A manchete da formadora de mercado reduzindo ofertas não trazia surpresa: mudou a profundidade do livro, não o valor justo. Spread e slippage ficaram iguais, mas cada barra passou a aceitar menos contratos e o range ficou mais nervoso — ordens grandes executaram em partes e stops saíram além do preço. Depois a liquidez voltou com uma surpresa positiva modesta e o mercado saiu do range em uma direção sorteada, mais vezes para cima. Quem comprou com limitada na zona e saiu com alvo pagou uma fração do custo de quem usou ordens a mercado.'
	},
	en: {
		title: 'Mission 06 · Thin book',
		tagline: 'A quiet market with few quotes resting in the book. Every entry and every exit has a cost.',
		objectives: [
			'Enter and exit with limit orders at the zones, and compare the spread and slippage line in the debrief.',
			'Planned risk of at most 1% of equity per idea.',
			'An unfilled limit is a missed trade, not a loss.'
		],
		topic: 'Structure and Orders · zones, limit vs market, spread and capacity',
		reveal:
			'This scenario was designed as a THIN BOOK: support and resistance were zones, and every market order paid spread and slippage (about 0.6% per round trip, a quarter of the range). The market maker pulling quotes carried no surprise: it changed the depth of the book, not fair value. Spread and slippage stayed the same, but each bar took fewer contracts and the range turned jumpier — large orders filled in pieces and stops filled beyond their price. Then liquidity returned with a modest positive surprise and the market left the range in a drawn direction, more often up. Buying with a limit at the zone and exiting at a target cost a fraction of what market orders paid.'
	}
};

texts['m07-trend-trailing'] = {
	'pt-BR': {
		title: 'Missão 07 · Tamanho e saída',
		tagline: 'Um mercado lateral, por enquanto. Quanto arriscar e quando sair fica com você.',
		objectives: [
			'Risco de até 1% por ideia, com a quantidade calculada pelo stop.',
			'No lucro, leve o stop à entrada ou use stop móvel, mas nunca mais apertado que o ruído (ATR).',
			'Termine sem drawdown acima de 10%.'
		],
		topic: 'Dimensionamento de risco · stop, stop móvel e múltiplos de R',
		reveal:
			'Este cenário foi desenhado como uma TENDÊNCIA com recuos: depois do range, uma manchete crível de adoção subiu o valor justo e a direção da tendência foi sorteada inclinada por ela — para cima em cerca de 6 de cada 10 partidas, nunca em todas. Os recuos até a média tiraram da posição quem usou stop móvel de 1×ATR; um stop móvel de 3×ATR devolveu parte do ganho, mas ficou no movimento. Um alvo fixo de 1,5R acertou mais vezes que o stop móvel, mas ganhou bem menos quando a tendência veio. No fim, um topo (ou fundo) nervoso e de volta ao range. O tamanho pelo stop, não a alavancagem, decidiu quanto cada erro custou.'
	},
	en: {
		title: 'Mission 07 · Size and exit',
		tagline: 'A sideways market, for now. How much to risk and when to get out is up to you.',
		objectives: [
			'Risk at most 1% per idea, with the quantity set by the stop.',
			'Once in profit, move the stop to break-even or trail it, but never tighter than the noise (ATR).',
			'Finish without a drawdown above 10%.'
		],
		topic: 'Risk sizing · stops, trailing stops and R multiples',
		reveal:
			'This scenario was designed as a TREND with pullbacks: after the range, a credible adoption headline lifted fair value and the trend’s direction was drawn tilted by it — up in about 6 of 10 runs, never in all. Pullbacks to the average took out 1×ATR trailing stops; a 3×ATR trail gave some back but stayed in the move. A fixed 1.5R target won more often than the trail but earned much less when the trend came. At the end, a choppy top (or bottom) and back to a range. Sizing from the stop, not leverage, decided what each mistake cost.'
	}
};

texts['m08-regime-switch'] = {
	'pt-BR': {
		title: 'Missão 08 · Sinais',
		tagline: 'Um mercado que muda de humor sem avisar. Que ferramenta serve agora?',
		objectives: [
			'Antes de cada entrada, escreva no plano em que regime você acha que está e por quê.',
			'Use uma ferramenta por regime.',
			'Risco planejado de até 1% do patrimônio.'
		],
		topic: 'Padrões e Indicadores · reversão × tendência, ADX e largura das bandas',
		reveal:
			'Este cenário foi desenhado como TROCA DE REGIME: o mercado alternou entre lateral, tendência (de direção sorteada a cada vez) e volatilidade alta, num caminho sorteado a cada partida e sem manchete avisando. Vender sobrecompra e comprar sobrevenda (RSI, Bollinger) funcionou na lateral e perdeu na tendência; seguir MACD ou a média funcionou na tendência e sangrou na lateral. ADX e largura das bandas ajudaram a separar os dois, mas mudaram tarde: o mercado já tinha trocado. A faixa e a tabela abaixo mostram os regimes desta partida e como cada leitura se saiu em cada um.'
	},
	en: {
		title: 'Mission 08 · Signals',
		tagline: 'A market that changes its mood without warning. Which tool fits now?',
		objectives: [
			'Before each entry, write in the plan which regime you think you are in, and why.',
			'Use one tool per regime.',
			'Planned risk of at most 1% of equity.'
		],
		topic: 'Patterns & Indicators · mean reversion vs trend, ADX and band width',
		reveal:
			'This scenario was designed as a REGIME SWITCH: the market moved between ranges, trends (direction drawn each time) and high volatility, on a path drawn per run and with no headline warning. Fading RSI or Bollinger extremes worked in ranges and lost in trends; following MACD or the average worked in trends and bled in ranges. ADX and band width helped tell the two apart, but late: the market had already switched. The band and the table below show this run’s regimes and how each read did in each of them.'
	}
};

// Phase-2 batch B (design D8.2). m09 assesses Options II with m04, m10 Options III with m05, m11
// Events and m12 Evaluation (the capstone, last in the campaign). The reveal points at the per-release
// table and, for m12, the campaign report that the debrief draws.
texts['m09-event-straddle'] = {
	'pt-BR': {
		title: 'Missão 09 · Véspera',
		tagline: 'Uma decisão de juros está na agenda, com o consenso publicado. O que ela vai dizer, ninguém sabe.',
		objectives: [
			'Antes de comprar, calcule os dois pontos de equilíbrio do straddle ou do strangle.',
			'Escreva no plano o cenário em que o trade perde (sem surpresa, queda da IV).',
			'Não pague mais que 2% do patrimônio em prêmio.'
		],
		topic: 'Opções II · volatilidade implícita × realizada, straddles e theta',
		reveal:
			'Este cenário foi desenhado como uma VÉSPERA DE EVENTO: a decisão foi anunciada 20 barras antes com consenso de "juros mantidos", e a volatilidade implícita subiu enquanto o range se comprimia. O resultado foi sorteado: igual ao consenso em 4 de cada 10 partidas, uma surpresa leve em 4 e uma forte em 2, com tamanho também sorteado. O straddle comprado antes pagou quando o movimento realizado superou o movimento implícito no prêmio (em geral só com surpresa forte); sem surpresa, theta e a queda da IV depois da divulgação levaram o prêmio. Comprar depois da divulgação pagou a IV ainda alta e perdeu o gap. A tabela abaixo mostra o que esta partida sorteou e o movimento implícito contra o realizado.'
	},
	en: {
		title: 'Mission 09 · The day before',
		tagline: 'A rate decision is on the calendar, with its consensus published. What it will say, nobody knows.',
		objectives: [
			'Before buying, compute the straddle’s or strangle’s two breakevens.',
			'Write in the plan the scenario in which the trade loses (no surprise, falling IV).',
			'Do not pay more than 2% of equity in premium.'
		],
		topic: 'Options II · implied vs realized volatility, straddles and theta',
		reveal:
			'This scenario was designed as THE DAY BEFORE AN EVENT: the decision was announced 20 bars ahead with a consensus of "rates on hold", and implied volatility rose while the range compressed. The outcome was drawn: as expected in 4 of 10 runs, a mild surprise in 4 and a strong one in 2, with a drawn size too. A straddle bought before paid when the realized move beat the move implied in its premium (usually only on a strong surprise); without a surprise, theta and the IV collapse after the release took the premium. Buying after the release paid for IV still high and missed the gap. The table below shows what this run drew and the implied against the realized move.'
	}
};

texts['m10-hedged-book'] = {
	'pt-BR': {
		title: 'Missão 10 · Proteção',
		tagline: 'Uma alta firme, com muita gente comprada. Quanto custa dormir tranquilo?',
		objectives: [
			'Antes de operar, escreva a perda máxima de cada estrutura (put de proteção, collar, spread).',
			'Mantenha folga sobre a margem de manutenção das opções vendidas.',
			'Compare o funding que você paga com o theta que você paga.'
		],
		topic: 'Opções III · put de proteção, collar, venda coberta, spreads e carrego',
		reveal:
			'Este cenário foi desenhado como um LIVRO PROTEGIDO: uma alta com longs lotados pagando funding a cada 4 barras e um rumor regulatório sem aviso, numa barra que muda a cada partida — às vezes depois do primeiro vencimento. A gravidade foi sorteada: forte em cerca de 45% das partidas, leve em 35% e nenhuma em 20%. A put de proteção pôs um piso de custo conhecido; o collar financiou a put abrindo mão da alta; uma put curta demais podia vencer na véspera do choque. Quem vendeu puts em excesso levou chamada de margem quando o rumor era forte, e quem segurou long alavancado com stop viu o stop executar bem além do preço no gap. Sem choque, a proteção foi só custo — o que também faz parte da lição.'
	},
	en: {
		title: 'Mission 10 · Cover',
		tagline: 'A steady rally, with many traders long. What does it cost to sleep well?',
		objectives: [
			'Before trading, write each structure’s maximum loss (protective put, collar, spread).',
			'Keep a buffer above the maintenance margin of written options.',
			'Compare the funding you pay with the theta you pay.'
		],
		topic: 'Options III · protective puts, collars, covered calls, spreads and carry',
		reveal:
			'This scenario was designed as a HEDGED BOOK: a rally with crowded longs paying funding every 4 bars and an unannounced regulation rumor, on a bar that changes from run to run — sometimes after the first expiry. Its severity was drawn: strong in about 45% of runs, mild in 35% and none in 20%. A protective put set a floor at a known cost; a collar financed the put by giving up the upside; a put that was too short-dated could expire the bar before the shock. Oversized put writers were margin-called when the rumor was strong, and a leveraged long with a stop saw it fill far beyond its price in the gap. With no shock, the hedge was only a cost — which is part of the lesson too.'
	}
};

texts['m11-news-calendar'] = {
	'pt-BR': {
		title: 'Missão 11 · Agenda',
		tagline: 'Três divulgações estão na agenda, cada uma com seu consenso. O mercado não espera por elas.',
		objectives: [
			'Antes de cada divulgação, decida ficar fora, pequeno ou protegido, e registre por quê.',
			'Não opere a manchete depois do gap sem um motivo novo.',
			'Risco planejado de até 1%, contando o gap.'
		],
		topic: 'Eventos · expectativa × surpresa, gaps e stops que saltam',
		reveal:
			'Este cenário foi desenhado como uma AGENDA: um índice de atividade, uma decisão de juros e uma atualização de rede foram anunciados de 15 a 25 barras antes, com consenso, e caíram no regime em que o caminho sorteado estava. O consenso já estava no preço: só a surpresa moveu o valor justo, quase toda no gap da abertura. Os resultados foram sorteados em torno do consenso, a atualização podia atrasar, e a reação de curto prazo também foi sorteada — notícia boa às vezes caiu. Um incidente sem aviso foi real em metade das partidas. A tabela abaixo mostra, para cada divulgação, consenso, resultado, gap, reação e se você estava posicionado antes ou entrou depois.'
	},
	en: {
		title: 'Mission 11 · Calendar',
		tagline: 'Three releases are on the calendar, each with its consensus. The market does not wait for them.',
		objectives: [
			'Before each release, decide to be flat, small or hedged, and record why.',
			'Do not trade the headline after the gap without a new reason.',
			'Planned risk of at most 1%, counting the gap.'
		],
		topic: 'Events · expectation vs surprise, gaps and jumping stops',
		reveal:
			'This scenario was designed as a CALENDAR: an activity index, a rate decision and a network upgrade were announced 15 to 25 bars ahead, with a consensus, and landed in whatever regime the drawn path was in. The consensus was already in the price: only the surprise moved fair value, almost all of it in the opening gap. Outcomes were drawn around the consensus, the upgrade could slip, and the short-term reaction was drawn too — good news sometimes sold off. An unannounced incident was real in half the runs. The table below shows, for each release, the consensus, the outcome, the gap, the reaction and whether you were positioned before or entered after.'
	}
};

texts['m12-capstone'] = {
	'pt-BR': {
		title: 'Missão 12 · Prova final',
		tagline: 'Tudo o que você viu até aqui, num só mercado. Uma missão é uma amostra.',
		objectives: [
			'Termine a missão.',
			'Cada entrada com plano e stop.',
			'Nenhuma liquidação e nenhuma chamada de margem.',
			'Leia o relatório da campanha antes de concluir qualquer coisa.'
		],
		topic: 'Avaliação · identificar o regime, dimensionar pelo pior caso, comparar muitas tentativas',
		reveal:
			'Este cenário foi desenhado como a PROVA FINAL: um caminho sorteado de laterais, tendências, volatilidade alta, livro fino e uma lateral com shorts lotados, onde quem ficava comprado recebia funding — com o risco de o caminho virar tendência de baixa. Uma decisão de juros foi anunciada com consenso; uma falha de rede sem aviso não mudou nada em 6 de cada 10 partidas. Nenhuma regra fixa (comprar e segurar, vender sobrecompra, seguir o MACD, vender straddle, carregar o funding) ganhou na maioria esmagadora das partidas; ler o regime antes, arriscar 1% e ficar fora da divulgação perdeu menos por unidade de drawdown, e ainda perdeu em parte delas. O relatório da campanha mostra seus resultados em todas as missões: com menos de 30 trades, é cedo para concluir.'
	},
	en: {
		title: 'Mission 12 · Final exam',
		tagline: 'Everything you have seen so far, in one market. One mission is one sample.',
		objectives: [
			'Finish the mission.',
			'Every entry with a plan and a stop.',
			'No liquidation and no margin call.',
			'Read the campaign report before concluding anything.'
		],
		topic: 'Evaluation · identify the regime, size for the worst case, compare many attempts',
		reveal:
			'This scenario was designed as the FINAL EXAM: a drawn path of ranges, trends, high volatility, a thin book and a crowded-short range where longs collected funding — at the risk of the path turning into a downtrend. A rate decision was announced with a consensus; an unannounced network outage changed nothing in 6 of 10 runs. No fixed rule (buy and hold, fade overbought, follow MACD, sell a straddle, carry the funding) won in an overwhelming share of runs; reading the regime first, risking 1% and staying flat through the release lost less per unit of drawdown, and still lost in some of them. The campaign report shows your results across every mission: with fewer than 30 trades it is too early to conclude.'
	}
};

/** The capstone: its debrief carries the campaign report (design D8.2 m12). */
export const CAPSTONE_MISSION = 'm12-capstone';

// Practice runs on imported packs (scenario ids "csv-…"): the player already knows the market, so
// the texts stay generic; the debrief shows the engine's reveal instead of a designed lesson.
const PRACTICE: Record<Locale, MissionText> = {
	'pt-BR': {
		title: 'Treino · mercado importado',
		tagline:
			'Um mercado real que você importou, mascarado (preço base 100 na primeira barra jogável). Você já conhece este cenário: pratique o processo.',
		objectives: COMMON_PT,
		topic: 'Prática livre · futuros perpétuos',
		reveal: ''
	},
	en: {
		title: 'Practice · imported market',
		tagline:
			'A real market you imported, masked (price base 100 at the first playable bar). You already know this scenario: practice your process.',
		objectives: COMMON_EN,
		topic: 'Free practice · perpetual futures',
		reveal: ''
	}
};

export function isPracticeScenario(id: string): boolean {
	return id.startsWith('csv-');
}

export function missionText(id: string, locale: Locale): MissionText {
	if (isPracticeScenario(id)) return PRACTICE[locale];
	return texts[id]?.[locale] ?? { title: id, tagline: '', objectives: [], topic: '', reveal: '' };
}
