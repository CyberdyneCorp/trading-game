// Player-facing mission texts, keyed by engine mission id (engine/src/missions.cpp). Objectives are
// engine data (MissionRules, task 4.1), rendered by objectives.ts.
// Titles and taglines are deliberately neutral: they must not reveal what the market will do.
// What the scenario was designed to teach is revealed only in the debrief (`reveal`).

import type { Locale, MessageKey } from './i18n.svelte';
import type { PackOrigin } from './storage.ts';
import type { PackText } from '#lib/engine/types.ts';

export interface MissionText {
	title: string;
	tagline: string;
	topic: string;
	reveal: string;
}

const texts: Record<string, Record<Locale, MissionText>> = {
	'm01-false-breakout': {
		'pt-BR': {
			title: 'Missão 01 · Range',
			tagline: 'O mercado passou semanas andando de lado. O que vem a seguir é com você.',
			topic: 'Futuros I · long e short, ordens e stops',
			reveal:
				'Este cenário foi desenhado como um ROMPIMENTO FALSO: o preço rompe o topo do range, atrai compradores, perde força e volta. Quem entrou no rompimento sem stop sofreu; quem esperou confirmação ou protegeu a posição limitou o dano. O rumor da alta já era esperado (sem surpresa) e não mudou o valor justo; o desmentido, crível e negativo, mudou — e foi precificado de uma vez, com gap na abertura seguinte: vender depois da manchete já era tarde. Dentro do range, comprar perto do suporte e vender perto da resistência teve vantagem pequena.'
		},
		en: {
			title: 'Mission 01 · Range',
			tagline: 'The market has been moving sideways for weeks. What comes next is up to you.',
			topic: 'Futures I · long and short, orders and stops',
			reveal:
				'This scenario was designed as a FALSE BREAKOUT: price breaks the top of the range, attracts buyers, stalls and falls back. Buying the breakout without a stop hurt; waiting for confirmation or protecting the position limited the damage. The rally rumor was already expected (no surprise) and did not move fair value; the credible negative denial did — and it was priced at once, with a gap at the next open: selling after the headline was already late. Inside the range, buying near support and selling near resistance had a small edge.'
		}
	},
	'm02-funding-squeeze': {
		'pt-BR': {
			title: 'Missão 02 · Momentum',
			tagline: 'O preço sobe com consistência. Observe também o que custa ficar posicionado.',
			topic: 'Futuros III · funding e custo de carregar posição',
			reveal:
				'Este cenário foi desenhado como um APERTO DE FUNDING: a alta veio com longs lotados pagando funding alto, seguida de uma correção. Carregar long por muito tempo custou caro mesmo quando o preço subiu. Na tendência, os recuos até a média (EMA) foram compras melhores que os topos, e funding extremo tornou possível um aperto contra os longs lotados — mas, até ele vir, a multidão costumava estar certa.'
		},
		en: {
			title: 'Mission 02 · Momentum',
			tagline: 'Price keeps grinding higher. Also watch what it costs to stay positioned.',
			topic: 'Futures III · funding and the cost of carry',
			reveal:
				'This scenario was designed as a FUNDING SQUEEZE: the rally came with crowded longs paying high funding, followed by a correction. Carrying a long for long cost a lot even while price rose. In the trend, pullbacks to the average (EMA) were better buys than the highs, and extreme funding made a squeeze against crowded longs possible — but until it came, the crowd was usually right.'
		}
	},
	'm03-volatility-shock': {
		'pt-BR': {
			title: 'Missão 03 · Mercado quieto',
			tagline: 'Volatilidade baixa, movimentos pequenos. Escolha sua alavancagem com cuidado.',
			topic: 'Futuros II · margem, alavancagem e preço de liquidação',
			reveal:
				'Este cenário foi desenhado como um CHOQUE DE VOLATILIDADE: depois de um longo período calmo, gaps e quedas bruscas atravessam stops e liquidam posições muito alavancadas. Antes do choque a volatilidade foi se comprimindo (Bandas de Bollinger e ATR estreitando): mercado quieto não é mercado seguro. A manchete saiu e a barra seguinte abriu com gap: stops e liquidações executaram no gap, não no preço marcado. Quando o choque chega muda a cada partida.'
		},
		en: {
			title: 'Mission 03 · Quiet market',
			tagline: 'Low volatility, small moves. Choose your leverage carefully.',
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
		topic: 'Opções I e II · payoff, prêmio, IV e gregas',
		reveal:
			'Este cenário foi desenhado com um EVENTO: a IV sobe antes do rompimento e despenca depois (IV crush). Quem comprou opções caras perto do evento pôde perder mesmo acertando a direção; quem comprou cedo ou com strike adequado aproveitou o movimento. A direção do rompimento não era garantida: a surpresa positiva da decisão inclinava as chances para cima, mas em parte das sementes o rompimento foi para baixo.'
	},
	en: {
		title: 'Mission 04 · Calls and puts',
		tagline: 'For the first time you can buy options. The premium is your maximum risk — choose carefully what you pay.',
		topic: 'Options I and II · payoff, premium, IV and Greeks',
		reveal:
			'This scenario was designed around an EVENT: IV rises before the breakout and collapses afterwards (IV crush). Buying expensive options near the event could lose even with the right direction; buying early or with a sensible strike captured the move. The breakout direction was not guaranteed: the decision’s positive surprise tilted the odds upward, but on some seeds the breakout went down.'
	}
};

texts['m05-options-writer'] = {
	'pt-BR': {
		title: 'Missão 05 · Vendendo prêmio',
		tagline: 'Agora você pode vender opções e receber o prêmio. Receber pouco para arriscar muito exige margem.',
		topic: 'Opções III · venda de opções, margem e chamada de margem',
		reveal:
			'Este cenário foi desenhado com prêmio farto em um mercado lateral seguido de um SALTO. Vender opções rendeu theta por muitas barras; sem tamanho controlado, o salto — um gap logo após a manchete da falha, em uma barra diferente a cada partida — gerou chamada de margem e recompra no pior momento.'
	},
	en: {
		title: 'Mission 05 · Selling premium',
		tagline: 'Now you can sell options and collect the premium. Collecting little to risk a lot needs margin.',
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
		topic: 'Estrutura e Ordens · zonas, limite × mercado, spread e capacidade',
		reveal:
			'Este cenário foi desenhado como um LIVRO FINO: suporte e resistência eram zonas, e cada ordem a mercado pagava spread e slippage (cerca de 0,6% ida e volta, um quarto do range). A manchete da formadora de mercado reduzindo ofertas não trazia surpresa: mudou a profundidade do livro, não o valor justo. Spread e slippage ficaram iguais, mas cada barra passou a aceitar menos contratos e o range ficou mais nervoso — ordens grandes executaram em partes e stops saíram além do preço. Depois a liquidez voltou com uma surpresa positiva modesta e o mercado saiu do range em uma direção sorteada, mais vezes para cima. Quem comprou com limitada na zona e saiu com alvo pagou uma fração do custo de quem usou ordens a mercado.'
	},
	en: {
		title: 'Mission 06 · Thin book',
		tagline: 'A quiet market with few quotes resting in the book. Every entry and every exit has a cost.',
		topic: 'Structure and Orders · zones, limit vs market, spread and capacity',
		reveal:
			'This scenario was designed as a THIN BOOK: support and resistance were zones, and every market order paid spread and slippage (about 0.6% per round trip, a quarter of the range). The market maker pulling quotes carried no surprise: it changed the depth of the book, not fair value. Spread and slippage stayed the same, but each bar took fewer contracts and the range turned jumpier — large orders filled in pieces and stops filled beyond their price. Then liquidity returned with a modest positive surprise and the market left the range in a drawn direction, more often up. Buying with a limit at the zone and exiting at a target cost a fraction of what market orders paid.'
	}
};

texts['m07-trend-trailing'] = {
	'pt-BR': {
		title: 'Missão 07 · Tamanho e saída',
		tagline: 'Um mercado lateral, por enquanto. Quanto arriscar e quando sair fica com você.',
		topic: 'Dimensionamento de risco · stop, stop móvel e múltiplos de R',
		reveal:
			'Este cenário foi desenhado como uma TENDÊNCIA com recuos: depois do range, uma manchete crível de adoção subiu o valor justo e a direção da tendência foi sorteada inclinada por ela — para cima em cerca de 6 de cada 10 partidas, nunca em todas. Os recuos até a média tiraram da posição quem usou stop móvel de 1×ATR; um stop móvel de 3×ATR devolveu parte do ganho, mas ficou no movimento. Um alvo fixo de 1,5R acertou mais vezes que o stop móvel, mas ganhou bem menos quando a tendência veio. No fim, um topo (ou fundo) nervoso e de volta ao range. O tamanho pelo stop, não a alavancagem, decidiu quanto cada erro custou.'
	},
	en: {
		title: 'Mission 07 · Size and exit',
		tagline: 'A sideways market, for now. How much to risk and when to get out is up to you.',
		topic: 'Risk sizing · stops, trailing stops and R multiples',
		reveal:
			'This scenario was designed as a TREND with pullbacks: after the range, a credible adoption headline lifted fair value and the trend’s direction was drawn tilted by it — up in about 6 of 10 runs, never in all. Pullbacks to the average took out 1×ATR trailing stops; a 3×ATR trail gave some back but stayed in the move. A fixed 1.5R target won more often than the trail but earned much less when the trend came. At the end, a choppy top (or bottom) and back to a range. Sizing from the stop, not leverage, decided what each mistake cost.'
	}
};

texts['m08-regime-switch'] = {
	'pt-BR': {
		title: 'Missão 08 · Sinais',
		tagline: 'Um mercado que muda de humor sem avisar. Que ferramenta serve agora?',
		topic: 'Padrões e Indicadores · reversão × tendência, ADX e largura das bandas',
		reveal:
			'Este cenário foi desenhado como TROCA DE REGIME: o mercado alternou entre lateral, tendência (de direção sorteada a cada vez) e volatilidade alta, num caminho sorteado a cada partida e sem manchete avisando. Vender sobrecompra e comprar sobrevenda (RSI, Bollinger) funcionou na lateral e perdeu na tendência; seguir MACD ou a média funcionou na tendência e sangrou na lateral. ADX e largura das bandas ajudaram a separar os dois, mas mudaram tarde: o mercado já tinha trocado. A faixa e a tabela abaixo mostram os regimes desta partida e como cada leitura se saiu em cada um.'
	},
	en: {
		title: 'Mission 08 · Signals',
		tagline: 'A market that changes its mood without warning. Which tool fits now?',
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
		topic: 'Opções II · volatilidade implícita × realizada, straddles e theta',
		reveal:
			'Este cenário foi desenhado como uma VÉSPERA DE EVENTO: a decisão foi anunciada 20 barras antes com consenso de "juros mantidos", e a volatilidade implícita subiu enquanto o range se comprimia. O resultado foi sorteado: igual ao consenso em 4 de cada 10 partidas, uma surpresa leve em 4 e uma forte em 2, com tamanho também sorteado. O straddle comprado antes pagou quando o movimento realizado superou o movimento implícito no prêmio (em geral só com surpresa forte); sem surpresa, theta e a queda da IV depois da divulgação levaram o prêmio. Comprar depois da divulgação pagou a IV ainda alta e perdeu o gap. A tabela abaixo mostra o que esta partida sorteou e o movimento implícito contra o realizado.'
	},
	en: {
		title: 'Mission 09 · The day before',
		tagline: 'A rate decision is on the calendar, with its consensus published. What it will say, nobody knows.',
		topic: 'Options II · implied vs realized volatility, straddles and theta',
		reveal:
			'This scenario was designed as THE DAY BEFORE AN EVENT: the decision was announced 20 bars ahead with a consensus of "rates on hold", and implied volatility rose while the range compressed. The outcome was drawn: as expected in 4 of 10 runs, a mild surprise in 4 and a strong one in 2, with a drawn size too. A straddle bought before paid when the realized move beat the move implied in its premium (usually only on a strong surprise); without a surprise, theta and the IV collapse after the release took the premium. Buying after the release paid for IV still high and missed the gap. The table below shows what this run drew and the implied against the realized move.'
	}
};

texts['m10-hedged-book'] = {
	'pt-BR': {
		title: 'Missão 10 · Proteção',
		tagline: 'Uma alta firme, com muita gente comprada. Quanto custa dormir tranquilo?',
		topic: 'Opções III · put de proteção, collar, venda coberta, spreads e carrego',
		reveal:
			'Este cenário foi desenhado como um LIVRO PROTEGIDO: uma alta com longs lotados pagando funding a cada 4 barras e um rumor regulatório sem aviso, numa barra que muda a cada partida — às vezes depois do primeiro vencimento. A gravidade foi sorteada: forte em cerca de 45% das partidas, leve em 35% e nenhuma em 20%. A put de proteção pôs um piso de custo conhecido; o collar financiou a put abrindo mão da alta; uma put curta demais podia vencer na véspera do choque. Quem vendeu puts em excesso levou chamada de margem quando o rumor era forte, e quem segurou long alavancado com stop viu o stop executar bem além do preço no gap. Sem choque, a proteção foi só custo — o que também faz parte da lição.'
	},
	en: {
		title: 'Mission 10 · Cover',
		tagline: 'A steady rally, with many traders long. What does it cost to sleep well?',
		topic: 'Options III · protective puts, collars, covered calls, spreads and carry',
		reveal:
			'This scenario was designed as a HEDGED BOOK: a rally with crowded longs paying funding every 4 bars and an unannounced regulation rumor, on a bar that changes from run to run — sometimes after the first expiry. Its severity was drawn: strong in about 45% of runs, mild in 35% and none in 20%. A protective put set a floor at a known cost; a collar financed the put by giving up the upside; a put that was too short-dated could expire the bar before the shock. Oversized put writers were margin-called when the rumor was strong, and a leveraged long with a stop saw it fill far beyond its price in the gap. With no shock, the hedge was only a cost — which is part of the lesson too.'
	}
};

texts['m11-news-calendar'] = {
	'pt-BR': {
		title: 'Missão 11 · Agenda',
		tagline: 'Três divulgações estão na agenda, cada uma com seu consenso. O mercado não espera por elas.',
		topic: 'Eventos · expectativa × surpresa, gaps e stops que saltam',
		reveal:
			'Este cenário foi desenhado como uma AGENDA: um índice de atividade, uma decisão de juros e uma atualização de rede foram anunciados de 15 a 25 barras antes, com consenso, e caíram no regime em que o caminho sorteado estava. O consenso já estava no preço: só a surpresa moveu o valor justo, quase toda no gap da abertura. Os resultados foram sorteados em torno do consenso, a atualização podia atrasar, e a reação de curto prazo também foi sorteada — notícia boa às vezes caiu. Um incidente sem aviso foi real em metade das partidas. A tabela abaixo mostra, para cada divulgação, consenso, resultado, gap, reação e se você estava posicionado antes ou entrou depois.'
	},
	en: {
		title: 'Mission 11 · Calendar',
		tagline: 'Three releases are on the calendar, each with its consensus. The market does not wait for them.',
		topic: 'Events · expectation vs surprise, gaps and jumping stops',
		reveal:
			'This scenario was designed as a CALENDAR: an activity index, a rate decision and a network upgrade were announced 15 to 25 bars ahead, with a consensus, and landed in whatever regime the drawn path was in. The consensus was already in the price: only the surprise moved fair value, almost all of it in the opening gap. Outcomes were drawn around the consensus, the upgrade could slip, and the short-term reaction was drawn too — good news sometimes sold off. An unannounced incident was real in half the runs. The table below shows, for each release, the consensus, the outcome, the gap, the reaction and whether you were positioned before or entered after.'
	}
};

texts['m12-capstone'] = {
	'pt-BR': {
		title: 'Missão 12 · Prova final',
		tagline: 'Tudo o que você viu até aqui, num só mercado. Uma missão é uma amostra.',
		topic: 'Avaliação · identificar o regime, dimensionar pelo pior caso, comparar muitas tentativas',
		reveal:
			'Este cenário foi desenhado como a PROVA FINAL: um caminho sorteado de laterais, tendências, volatilidade alta, livro fino e uma lateral com shorts lotados, onde quem ficava comprado recebia funding — com o risco de o caminho virar tendência de baixa. Uma decisão de juros foi anunciada com consenso; uma falha de rede sem aviso não mudou nada em 6 de cada 10 partidas. Nenhuma regra fixa (comprar e segurar, vender sobrecompra, seguir o MACD, vender straddle, carregar o funding) ganhou na maioria esmagadora das partidas; ler o regime antes, arriscar 1% e ficar fora da divulgação perdeu menos por unidade de drawdown, e ainda perdeu em parte delas. O relatório da campanha mostra seus resultados em todas as missões: com menos de 30 trades, é cedo para concluir.'
	},
	en: {
		title: 'Mission 12 · Final exam',
		tagline: 'Everything you have seen so far, in one market. One mission is one sample.',
		topic: 'Evaluation · identify the regime, size for the worst case, compare many attempts',
		reveal:
			'This scenario was designed as the FINAL EXAM: a drawn path of ranges, trends, high volatility, a thin book and a crowded-short range where longs collected funding — at the risk of the path turning into a downtrend. A rate decision was announced with a consensus; an unannounced network outage changed nothing in 6 of 10 runs. No fixed rule (buy and hold, fade overbought, follow MACD, sell a straddle, carry the funding) won in an overwhelming share of runs; reading the regime first, risking 1% and staying flat through the release lost less per unit of drawdown, and still lost in some of them. The campaign report shows your results across every mission: with fewer than 30 trades it is too early to conclude.'
	}
};

// Strategies (add-academy-and-strategies design D8): option structures after Options III, before the
// capstone. Titles stay neutral; the reveal says what the seeds drew and what each structure did over
// the learnability seeds (engine/tests/test_mission_learnability_c.cpp).
texts['m13-call-spread'] = {
	'pt-BR': {
		title: 'Missão 13 · Trava',
		tagline: 'As opções estão caras depois de um período quieto. Comprar a call inteira ou montar uma trava?',
		topic: 'Estratégias · trava de alta com calls contra a call a seco',
		reveal:
			'Este cenário foi desenhado como TRAVA contra CALL A SECO: depois de um range com volatilidade implícita alta, uma manchete crível de adoção, de força sorteada, inclinou para cima uma tendência de direção sorteada — para cima na maioria das partidas, nunca em todas, e quase sempre modesta. A trava (call comprada no dinheiro e call vendida um strike acima) custou pouco mais da metade da call e rendeu pelo menos o mesmo que ela quase sempre que a alta ficou abaixo da largura da trava; quando as duas perderam, a trava perdeu menos. Nas partidas de alta grande, a call a seco ganhou mais: vender a call de cima abriu mão da alta além dela. A faixa e a tabela abaixo mostram o regime e a manchete desta partida.'
	},
	en: {
		title: 'Mission 13 · Spread',
		tagline: 'Options are expensive after a quiet spell. Buy the whole call or build a spread?',
		topic: 'Strategies · a bull call spread against a naked call',
		reveal:
			'This scenario was designed as a SPREAD against a NAKED CALL: after a range with high implied volatility, a credible adoption headline of drawn strength tilted a trend of drawn direction upward — up in most runs, never in all, and usually modest. The spread (an at-the-money call bought and a call one strike up written) cost a little over half the call and returned at least as much as the call almost every time the rally stayed below the spread’s width; when both lost, the spread lost less. In runs with a large rally, the naked call made more: writing the upper call gave away the upside beyond it. The band and the table below show this run’s regime and headline.'
	}
};

texts['m14-covered-call'] = {
	'pt-BR': {
		title: 'Missão 14 · Renda',
		tagline: 'Um mercado que anda devagar, com um vencimento a cada 24 barras. Dá para tirar renda da posição?',
		topic: 'Estratégias · venda coberta: renda contra alta limitada',
		reveal:
			'Este cenário foi desenhado como RENDA com risco de ALTA FORTE: o mercado andou devagar e, em cerca de 1 a cada 3 partidas, uma manchete de adoção sem aviso, numa barra sorteada, disparou uma alta forte. Sem a alta, vender uma call cerca de 5% acima a cada vencimento rendeu mais que o long sozinho em quase 9 de cada 10 partidas; com a alta, o long sozinho ganhou mais em 8 de cada 10, porque a call vendida limitou o ganho. Nas quedas o prêmio amorteceu pouco: a pior partida da venda coberta perdeu quase tanto quanto a do long. A call vendida teve margem própria a missão inteira, e o long pagou ou recebeu funding. A tabela abaixo mostra se esta partida sorteou a alta.'
	},
	en: {
		title: 'Mission 14 · Income',
		tagline: 'A slow market, with an expiry every 24 bars. Can the position earn an income?',
		topic: 'Strategies · covered calls: income against a capped upside',
		reveal:
			'This scenario was designed as INCOME with the risk of a STRONG RALLY: the market moved slowly and, in about 1 run in 3, an unannounced adoption headline on a drawn bar set off a strong rally. Without the rally, writing a call about 5% above the price at each expiry earned more than the long alone in almost 9 runs in 10; with the rally, the long alone made more in 8 in 10, because the written call capped the gain. In falls the premium cushioned little: the covered call’s worst run lost almost as much as the long’s. The written call carried its own margin throughout, and the long paid or earned funding. The table below shows whether this run drew the rally.'
	}
};

texts['m15-protective-put'] = {
	'pt-BR': {
		title: 'Missão 15 · Seguro',
		tagline: 'Uma alta firme. Proteger a posição custa um prêmio — vale a pena pagar?',
		topic: 'Estratégias · put de proteção: piso, franquia, prazo e custo',
		reveal:
			'Este cenário foi desenhado como um SEGURO: uma alta firme e, sem aviso, um incidente na corretora numa barra sorteada — grave em cerca de 3 de cada 10 partidas, leve em 3 e inofensivo em 4. O long com uma put no dinheiro do segundo vencimento nunca perdeu mais que o piso (entrada − strike + prêmio + custos). Sem incidente, a proteção foi só custo em quase todas as partidas, e na média das partidas a put perdeu dinheiro: seguro custa, na média. Um stop de 2×ATR não custou nada até o incidente grave, quando executou mais de 2% além do gatilho no gap. Uma put 5% fora do dinheiro custou cerca de 40% da put no dinheiro, com um piso mais baixo. A tabela abaixo mostra o incidente desta partida.'
	},
	en: {
		title: 'Mission 15 · Insurance',
		tagline: 'A steady rally. Protecting the position costs a premium — is it worth paying?',
		topic: 'Strategies · protective puts: floor, deductible, tenor and cost',
		reveal:
			'This scenario was designed as INSURANCE: a steady rally and, unannounced, an exchange incident on a drawn bar — severe in about 3 runs in 10, mild in 3 and harmless in 4. The long with an at-the-money put of the second expiry never lost more than its floor (entry − strike + premium + costs). With no incident the hedge was only a cost in almost every run, and on average over the runs the put lost money: insurance costs, on average. A 2×ATR stop cost nothing until the severe incident, when it filled more than 2% beyond its trigger in the gap. A put 5% out of the money cost about 40% of the at-the-money put, with a lower floor. The table below shows this run’s incident.'
	}
};

texts['m16-range-condor'] = {
	'pt-BR': {
		title: 'Missão 16 · Prêmio',
		tagline: 'Um range longo e opções caras. Vender prêmio — com ou sem asas?',
		topic: 'Estratégias · iron condor contra strangle vendido: risco definido e risco aberto',
		reveal:
			'Este cenário foi desenhado como um RANGE seguido de um ROMPIMENTO: as opções ficaram caras em relação ao que o range realizou, e depois o mercado rompeu numa direção sorteada, antes ou depois do primeiro vencimento conforme a partida; um rumor regulatório sem aviso saiu no rompimento e, em metade das partidas, não mudou nada. Vender prêmio ganhou na maioria das partidas (o strangle e o condor, cerca de 2 em cada 3) e perdeu muito em algumas: a pior perda do strangle foi mais de 3 vezes a do condor na mesma quantidade, e a do condor nunca passou de largura − crédito. Um strangle grande demais levou chamada de margem em mais de 1 de cada 3 partidas em que o rompimento veio antes do vencimento. A faixa e a tabela abaixo mostram quando e como esta partida rompeu.'
	},
	en: {
		title: 'Mission 16 · Premium',
		tagline: 'A long range and expensive options. Sell premium — with or without wings?',
		topic: 'Strategies · an iron condor against a short strangle: defined against open risk',
		reveal:
			'This scenario was designed as a RANGE followed by a BREAKOUT: options were priced above what the range realized, then the market broke out in a drawn direction, before or after the first expiry depending on the run; an unannounced regulation rumor came out at the break and, in half the runs, changed nothing. Selling premium won in most runs (the strangle and the condor, about 2 in 3) and lost big in some: the strangle’s worst loss was more than 3 times the condor’s at the same quantity, and the condor’s never exceeded width − credit. An oversized strangle was margin-called in more than 1 in 3 runs where the breakout came before expiry. The band and the table below show when and how this run broke out.'
	}
};

texts['m17-before-expiry'] = {
	'pt-BR': {
		title: 'Missão 17 · Vencimento',
		tagline: 'Um vencimento a cada 25 barras. Quando o relógio aperta: fechar, rolar ou segurar?',
		topic: 'Estratégias · gestão antes do vencimento: theta, gama e rolagem',
		reveal:
			'Este cenário foi desenhado para a GESTÃO ANTES DO VENCIMENTO: o mercado alternou lateral, tendência e volatilidade alta num caminho sorteado, sem manchetes. Perto do vencimento, o theta por barra e o gama de uma opção no dinheiro quase sempre mais que dobraram entre 20 e 3 barras antes. 5 barras antes, restava só cerca de 12% do prêmio de uma put vendida no dinheiro para ganhar, enquanto uma barra ainda podia mexer nela tanto quanto no começo. Rolar 5 barras antes custou cerca de 3 vezes os custos de segurar: são duas operações a mais. Uma call fora do dinheiro segurada até o vencimento terminou sem valor em cerca de 7 de cada 10 partidas. A faixa abaixo mostra o caminho de regimes desta partida.'
	},
	en: {
		title: 'Mission 17 · Expiry',
		tagline: 'An expiry every 25 bars. When the clock runs down: close, roll or hold?',
		topic: 'Strategies · managing before expiry: theta, gamma and rolling',
		reveal:
			'This scenario was designed for MANAGING BEFORE EXPIRY: the market moved between ranges, trends and high volatility on a drawn path, with no headlines. Near expiry, the theta per bar and the gamma of an at-the-money option almost always more than doubled from 20 to 3 bars before. 5 bars before, only about 12% of a written at-the-money put’s premium was left to earn, while one bar could still move it as much as at the start. Rolling 5 bars before cost about 3 times the costs of holding: it is two more trades. An out-of-the-money call held to expiry ended worthless in about 7 runs in 10. The band below shows this run’s path of regimes.'
	}
};

texts['m18-gamma-scalp'] = {
	'pt-BR': {
		title: 'Missão 18 · Hedge',
		tagline: 'Opções compradas e um perpétuo para equilibrar o delta. Quanto este mercado vai se mexer?',
		topic: 'Opções avançadas · gamma scalping e hedge de delta',
		reveal:
			'Este cenário foi desenhado para o GAMMA SCALPING: um caminho sorteado alternou trechos agitados, que se mexeram mais do que as opções cobravam (volatilidade realizada acima da implícita), e trechos calmos, que se mexeram menos. Um straddle comprado no dinheiro e neutralizado em delta com o perpétuo numa faixa de delta ganhou na maioria das partidas em que a volatilidade realizada ficou pelo menos 1,2 vez acima da implícita, e perdeu na grande maioria das partidas em que ficou abaixo de 0,85 vez: o theta é o aluguel do gama. Neutralizar a cada barra pagou pelo menos o dobro de taxas de neutralizar na faixa. A faixa abaixo mostra o caminho de regimes desta partida.'
	},
	en: {
		title: 'Mission 18 · Hedge',
		tagline: 'Bought options and a perpetual to balance the delta. How much will this market move?',
		topic: 'Advanced options · gamma scalping and delta hedging',
		reveal:
			'This scenario was designed for GAMMA SCALPING: a drawn path alternated busy stretches, which moved more than the options priced in (realized volatility above implied), and calm stretches, which moved less. An at-the-money straddle bought and delta-hedged with the perpetual on a delta band made money in most runs where realized volatility was at least 1.2 times the implied, and lost in the great majority where it was below 0.85 times: theta is the rent paid for gamma. Hedging every bar paid at least twice the fees of hedging on the band. The band below shows this run’s path of regimes.'
	}
};

texts['m19-funding-carry'] = {
	'pt-BR': {
		title: 'Missão 19 · Carrego',
		tagline: 'Os longs pagam funding. Dá para receber esse funding sem apostar na direção?',
		topic: 'Opções avançadas · carrego de funding e paridade put-call',
		reveal:
			'Este cenário foi desenhado para o CARREGO DE FUNDING: os longs estavam lotados e pagavam funding; um short no perpétuo recebia, e um long sintético (call comprada e put vendida no mesmo strike e vencimento) tirava o risco de preço. Em parte das partidas a multidão desmontou: o funding virou negativo (os shorts passaram a pagar) e apertos levantaram o preço. Sem desmonte, o carrego protegido rendeu depois dos custos em pelo menos 3 de cada 4 partidas; com desmonte, perdeu na maioria. O hedge tira o risco de preço, não o de margem: a 5x, a perna do perpétuo foi liquidada em pelo menos 1 de cada 4 partidas com desmonte enquanto as opções ganhavam; a 2x, quase nunca.'
	},
	en: {
		title: 'Mission 19 · Carry',
		tagline: 'Longs pay funding. Can you collect it without betting on direction?',
		topic: 'Advanced options · funding carry and put-call parity',
		reveal:
			'This scenario was designed for FUNDING CARRY: longs were crowded and paid funding; a short perpetual collected it, and a synthetic long (a call bought and a put written at the same strike and expiry) removed the price risk. In some runs the crowd unwound: funding turned negative (shorts started paying) and squeezes lifted the price. Without an unwind, the hedged carry made money after costs in at least 3 runs in 4; with one, it lost in most. The hedge removes price risk, not margin risk: at 5x the perpetual leg was liquidated in at least 1 unwind run in 4 while the options gained; at 2x, almost never.'
	}
};

texts['m20-event-vol'] = {
	'pt-BR': {
		title: 'Missão 20 · Anúncio',
		tagline: 'Uma divulgação está marcada. As opções sabem disso — e você, sabe o que fazer com elas?',
		topic: 'Opções avançadas · volatilidade em torno de um evento',
		reveal:
			'Este cenário foi desenhado para a VOLATILIDADE EM TORNO DE UM EVENTO: a divulgação foi anunciada 35 barras antes. A volatilidade implícita subiu em dois degraus até ela e despencou logo depois — em todas as partidas, para no máximo 70% do nível da barra anterior —, qualquer que fosse o resultado. Comprar o straddle cedo e vendê-lo uma barra antes da divulgação superou segurá-lo através dela na maioria das partidas. Segurar através ganhou quase sempre que o movimento superou o preço pago pelo straddle (o movimento implícito), e perdeu na grande maioria das outras partidas.'
	},
	en: {
		title: 'Mission 20 · Announcement',
		tagline: 'A release is scheduled. The options know it — do you know what to do with them?',
		topic: 'Advanced options · volatility around an event',
		reveal:
			'This scenario was designed for VOLATILITY AROUND AN EVENT: the release was announced 35 bars ahead. Implied volatility rose in two steps toward it and collapsed right after — in every run, to at most 70% of the level the bar before —, whatever the outcome. Buying the straddle early and selling it the bar before the release beat holding it through in most runs. Holding through won almost always when the move beat the price paid for the straddle (the implied move), and lost in the great majority of the other runs.'
	}
};

texts['m21-calendar'] = {
	'pt-BR': {
		title: 'Missão 21 · Calendário',
		tagline: 'Três vencimentos, uma volatilidade estável. O tempo passa diferente em cada um.',
		topic: 'Opções avançadas · travas de calendário e diagonais',
		reveal:
			'Este cenário foi desenhado para o CALENDÁRIO: um range com volatilidade implícita estável e um rompimento de direção sorteada, antes ou depois do vencimento curto. No vencimento curto, a perda de um calendário no dinheiro nunca passou do débito pago mais os custos. Ele ganhou na maioria das partidas em que o preço terminou a até um degrau de strike do strike, e perdeu em quase todas em que terminou a mais de três degraus. A margem de calendário (a call longa cobrindo a curta) deixou a estrutura sem chamada de margem.'
	},
	en: {
		title: 'Mission 21 · Calendar',
		tagline: 'Three expiries, a steady volatility. Time passes differently in each one.',
		topic: 'Advanced options · calendar and diagonal spreads',
		reveal:
			'This scenario was designed for the CALENDAR: a range with steady implied volatility and a breakout of drawn direction, before or after the near expiry. At the near expiry, an at-the-money calendar’s loss never exceeded the debit paid plus the costs. It made money in most runs where the price ended within one strike step of the strike, and lost in almost all where it ended more than three steps away. Calendar margin (the long call covering the short one) left the structure without a margin call.'
	}
};

texts['m22-scale-manage'] = {
	'pt-BR': {
		title: 'Missão 22 · Gestão',
		tagline: 'Entrar é metade do trabalho. A outra metade é o que fazer depois.',
		topic: 'Gestão de trade · entradas em partes, saídas parciais e stop no ponto de entrada',
		reveal:
			'Este cenário foi desenhado para a GESTÃO DA POSIÇÃO: uma manchete sem aviso deu início a uma tendência com recuos; a perna seguinte foi sorteada de novo, então às vezes a tendência foi longe, às vezes parou e às vezes falhou. Realizar metade em +1R, levar o stop ao ponto de entrada e acompanhar o resto ganhou com mais frequência e com resultados mais estreitos que entrar e sair de uma vez em 2R; já o tudo-ou-nada em 2R rendeu mais na maioria das partidas em que a tendência alcançou o alvo. Cada forma de gestão perdeu em parte das partidas.'
	},
	en: {
		title: 'Mission 22 · Management',
		tagline: 'Entering is half the job. The other half is what you do next.',
		topic: 'Trade management · scaling in, partial exits and the stop to breakeven',
		reveal:
			'This scenario was designed for MANAGING THE POSITION: an unannounced headline started a trend with pullbacks; the next leg was drawn again, so the trend sometimes ran far, sometimes stalled and sometimes failed. Taking half at +1R, moving the stop to breakeven and trailing the rest won more often and with narrower results than all in and all out at 2R; all in and all out at 2R earned more in most runs where the trend reached the target. Every way of managing lost in some runs.'
	}
};

texts['m23-second-chance'] = {
	'pt-BR': {
		title: 'Missão 23 · Recomeço',
		tagline: 'Um mercado em range. Cada trade é uma decisão nova.',
		topic: 'Psicologia · disciplina depois de um stop',
		reveal:
			'Este cenário foi desenhado como um RECOMEÇO: um rompimento limpo no começo falhou rápido e parou muitos stops; depois veio um trecho picotado, e só mais tarde um movimento de verdade, de direção sorteada. Esperar pelo menos 3 barras depois do stop e voltar só no próximo sinal, com o tamanho normal, perdeu menos que voltar na hora com o dobro do tamanho em pelo menos 7 de cada 10 partidas. Os vieses que o seu registro mostra estão em “O que aconteceu”, com as barras em que aconteceram.'
	},
	en: {
		title: 'Mission 23 · Restart',
		tagline: 'A ranging market. Every trade is a new decision.',
		topic: 'Psychology · discipline after a stop',
		reveal:
			'This scenario was designed as a RESTART: an early clean-looking breakout failed fast and stopped many players out; choppy trading followed, and only later a real move of drawn direction. Waiting at least 3 bars after the stop and coming back only on the next signal, at normal size, lost less than coming back at once at twice the size in at least 7 runs in 10. The biases your log shows are under “What happened”, with the bars they happened on.'
	}
};

texts['m24-fast-market'] = {
	'pt-BR': {
		title: 'Missão 24 · Disparada',
		tagline: 'Um mercado quieto. Até que deixa de ser.',
		topic: 'Psicologia · planejar antes do movimento, não durante',
		reveal:
			'Este cenário foi desenhado como uma DISPARADA: depois de um range quieto, uma alta íngreme com volume pesado e funding extremo, que nenhuma notícia sustentava. Na maioria das partidas ela devolveu a alta; em algumas continuou. Entrar no rompimento do range com o stop abaixo dele rendeu mais R que perseguir o preço depois de 4 altas seguidas, longe da média, em cerca de 2 de cada 3 partidas — e a perseguição ainda ganhou em parte delas: correr atrás nem sempre está errado, só custa mais caro quando está. Os vieses que o seu registro mostra estão em “O que aconteceu”.'
	},
	en: {
		title: 'Mission 24 · Rally',
		tagline: 'A quiet market. Until it is not.',
		topic: 'Psychology · plan before the move, not during it',
		reveal:
			'This scenario was designed as a RALLY: after a quiet range, a steep climb on heavy volume and extreme funding that no headline supported. In most runs it gave the gains back; in some it kept going. Entering on the range break with the stop below it earned more R than chasing the price after 4 rising closes far from the average in about 2 runs in 3 — and the chase still won in some of them: chasing is not always wrong, it just costs more when it is. The biases your log shows are under “What happened”.'
	}
};

texts['m25-the-dip'] = {
	'pt-BR': {
		title: 'Missão 25 · Queda',
		tagline: 'Uma tendência com sustos no caminho. O que você faz no próximo?',
		topic: 'Psicologia · aumentar uma posição perdedora só com plano',
		reveal:
			'Este cenário foi desenhado como uma QUEDA: uma tendência de alta com quedas bruscas no caminho, que se recuperavam — até que, em cerca de 3 de cada 10 partidas, uma não se recuperou. Uma entrada única com stop planejado igualou ou superou o preço médio para baixo até o mesmo stop na maioria das partidas; o preço médio ganhou em algumas (a armadilha funciona às vezes), mas sua pior partida foi pelo menos o dobro da pior da entrada única. Os vieses que o seu registro mostra estão em “O que aconteceu”.'
	},
	en: {
		title: 'Mission 25 · Dip',
		tagline: 'A trend with scares along the way. What do you do at the next one?',
		topic: 'Psychology · adding to a loser only with a plan',
		reveal:
			'This scenario was designed as a DIP: an uptrend with sharp drops along the way that recovered — until, in about 3 runs in 10, one did not. A single entry with a planned stop matched or beat averaging down to the same stop in most runs; averaging down won in some (the trap works sometimes), but its worst run was at least twice as bad as the single entry’s worst. The biases your log shows are under “What happened”.'
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
		topic: 'Prática livre · futuros perpétuos',
		reveal: ''
	},
	en: {
		title: 'Practice · imported market',
		tagline:
			'A real market you imported, masked (price base 100 at the first playable bar). You already know this scenario: practice your process.',
		topic: 'Free practice · perpetual futures',
		reveal: ''
	}
};

// A pack downloaded from the catalog is not one the player imported (playtest: the briefing called
// it "a real market you imported").
const PRACTICE_CATALOG: Record<Locale, Pick<MissionText, 'title' | 'tagline'>> = {
	'pt-BR': {
		title: 'Treino · pacote do catálogo',
		tagline:
			'Um mercado do catálogo de pacotes, mascarado (preço base 100 na primeira barra jogável). É um cenário conhecido: pratique o processo.'
	},
	en: {
		title: 'Practice · catalog pack',
		tagline: 'A market from the pack catalog, masked (price base 100 at the first playable bar). It is a known scenario: practice your process.'
	}
};

/** Briefing line of a pack run: synthetic or a masked market, imported here or from the catalog. */
export function packModeKey(marketMode: string, origin: PackOrigin | null): MessageKey {
	const catalog = origin === 'catalog';
	if (marketMode === 'synthetic') return catalog ? 'brief.modeCatalogSynthetic' : 'brief.modePackSynthetic';
	return catalog ? 'brief.modeCatalog' : 'brief.modePack';
}

/**
 * Tag of a market: generated prices (a built-in mission or a synthetic pack) are SYNTHETIC; a masked
 * recorded market (historical or branch pack) is KNOWN. `mode` is the pack's, or the state's marketMode.
 */
export function marketTagKey(knownScenario: boolean, mode: string | undefined): MessageKey {
	return knownScenario && mode !== 'synthetic' ? 'top.known' : 'top.synthetic';
}

/** Where a run's pack came from: null for a mission run; a pack no longer listed was imported. */
export function packOriginOf(packs: readonly { digest: string; origin?: PackOrigin }[], digest: string | null | undefined): PackOrigin | null {
	if (!digest) return null;
	return packs.find((p) => p.digest === digest)?.origin ?? 'import';
}

export function isPracticeScenario(id: string): boolean {
	return id.startsWith('csv-');
}

// A pack generated from a built-in mission ("packer generate m09-event-straddle --seed 7" names its
// scenario "m09-event-straddle-s7") or from another source: it carries no texts of its own.
const GENERATED_ID = /^(m\d{2}-[a-z0-9-]+)-s\d+$/;
const PRACTICE_GENERATED: Record<Locale, MissionText & { from: string }> = {
	'pt-BR': {
		title: 'Treino · pacote sintético',
		tagline: 'Um mercado gerado distribuído em um pacote: preços simulados, não um ativo real. Pratique o processo.',
		topic: 'Prática livre · pacote sintético',
		reveal: '',
		from: 'pacote sintético'
	},
	en: {
		title: 'Practice · synthetic pack',
		tagline: 'A generated market shipped in a pack: simulated prices, not a real asset. Practice your process.',
		topic: 'Free practice · synthetic pack',
		reveal: '',
		from: 'synthetic pack'
	}
};

function packedMissionText(id: string, locale: Locale): MissionText {
	const { from, ...generic } = PRACTICE_GENERATED[locale];
	const base = texts[GENERATED_ID.exec(id)?.[1] ?? '']?.[locale];
	// The designed reveal describes the built-in run; a pack's debrief shows the engine's reveal.
	return base ? { ...base, title: `${generic.title.split(' · ')[0]} · ${base.title.split(' · ').pop()} (${from})`, reveal: '' } : generic;
}

/** Texts of a mission; for a practice pack run, `origin` says whether it was imported or downloaded. */
export function missionText(id: string, locale: Locale, origin: PackOrigin | null = null, packTexts?: readonly PackText[]): MissionText {
	return withPackTexts(baseMissionText(id, locale, origin), packTexts, locale);
}

function baseMissionText(id: string, locale: Locale, origin: PackOrigin | null): MissionText {
	if (isPracticeScenario(id)) return origin === 'catalog' ? { ...PRACTICE[locale], ...PRACTICE_CATALOG[locale] } : PRACTICE[locale];
	return texts[id]?.[locale] ?? packedMissionText(id, locale);
}

/**
 * A schema-v2 pack's own title and briefing (mission.json, checked by the engine: one entry per
 * locale) over the generic pack texts; the rest (topic, reveal) stays the generic one.
 */
export function withPackTexts(base: MissionText, packTexts: readonly PackText[] | undefined, locale: Locale): MissionText {
	const own = packTexts?.find((x) => x.locale === locale);
	return own ? { ...base, title: own.title, tagline: own.briefing } : base;
}

/** The mission texts of an installed pack (undefined for a mission run or a pack without them). */
export function packTextsOf(packs: readonly { digest: string; texts?: PackText[] }[], digest: string | null | undefined): PackText[] | undefined {
	return digest ? packs.find((p) => p.digest === digest)?.texts : undefined;
}
