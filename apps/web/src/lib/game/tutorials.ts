// Pre-mission tutorial cards (spec mission-tutorial "Tutorial before a mission").
// They teach the concept, what to watch and which controls to use. Like the mission titles, they
// must not reveal what the market will do: what a scenario was built to teach stays in the debrief.

import type { Locale } from './i18n.svelte';

export type TutorialArt =
	| 'chart'
	| 'ticket'
	| 'risk'
	| 'news'
	| 'plan'
	| 'funding'
	| 'trend'
	| 'leverage'
	| 'volatility'
	| 'payoff'
	| 'chain'
	| 'decay'
	| 'margin'
	| 'advisor'
	| 'calendar'
	| 'wire'
	| 'book'
	| 'trail'
	| 'regime'
	| 'straddle'
	| 'report'
	| 'spread'
	| 'covered'
	| 'insurance'
	| 'condor'
	| 'roll';

export interface TutorialCard {
	art: TutorialArt;
	title: string;
	/** At most three sentences; [[id|text]] links a glossary term (content/glossary.ts). */
	body: string;
}

type Cards = Record<Locale, TutorialCard[]>;

const m01: Cards = {
	'pt-BR': [
		{
			art: 'chart',
			title: 'Leia o gráfico',
			body: 'Cada candle é uma hora: abertura, máxima, mínima e fechamento. A área hachurada à direita é o futuro, que ninguém conhece. Espaço avança uma barra; Shift+Espaço avança cinco.'
		},
		{
			art: 'ticket',
			title: 'Long ou short',
			body: 'Comprar (long) ganha se o preço subir; vender (short) ganha se cair. Ordem a mercado executa na abertura da próxima barra; limite espera o seu preço. Aceita não significa executada: o feed avisa quando executar.'
		},
		{
			art: 'risk',
			title: 'O stop vem primeiro',
			body: 'Defina o stop antes da entrada: 1×ATR, 2×ATR, Swing, ou arraste a linha SL? no gráfico. Com "Dimensionar por risco", o stop decide a quantidade: 1% do patrimônio em jogo por ideia.'
		},
		{
			art: 'news',
			title: 'Notícias',
			body: 'Cada manchete mostra o esperado, o anunciado e a credibilidade. Uma surpresa crível é precificada de uma vez, com gap na abertura seguinte. Operar depois da manchete costuma ser tarde.'
		},
		{
			art: 'advisor',
			title: 'Plano, assessor e pontos',
			body: 'Antes de cada entrada você registra a hipótese e onde ela estaria errada; não operar também vale. O assessor lê o mercado visível por 1 ponto e acerta só um pouco mais que a metade das vezes. Cada trade fechado com plano e stop rende 1 ponto, mais 1 se lucrar, e terminar a missão rende 2.'
		}
	],
	en: [
		{
			art: 'chart',
			title: 'Read the chart',
			body: 'Each candle is one hour: open, high, low and close. The hatched area on the right is the future, which nobody knows. Space advances one bar; Shift+Space advances five.'
		},
		{
			art: 'ticket',
			title: 'Long or short',
			body: 'Buying (long) gains if price rises; selling (short) gains if it falls. A market order fills at the next bar’s open; a limit waits for your price. Accepted is not filled: the feed tells you when it fills.'
		},
		{
			art: 'risk',
			title: 'The stop comes first',
			body: 'Set the stop before the entry: 1×ATR, 2×ATR, Swing, or drag the SL? line on the chart. With "Size by risk", the stop sets the quantity: 1% of equity at stake per idea.'
		},
		{
			art: 'news',
			title: 'News',
			body: 'Each headline shows what was expected, what was announced and its credibility. A credible surprise is priced at once, with a gap at the next open. Trading after the headline is usually late.'
		},
		{
			art: 'advisor',
			title: 'Plan, advisor and points',
			body: 'Before each entry you record the thesis and where it would be wrong; not trading counts too. The advisor reads the visible market for 1 point and is right only a little more than half the time. Each closed trade with a plan and a stop earns 1 point, 1 more if it made money, and finishing the mission earns 2.'
		}
	]
};

const m02: Cards = {
	'pt-BR': [
		{
			art: 'funding',
			title: 'Funding',
			body: 'No futuro perpétuo, a cada 4 barras um lado paga o outro pela taxa publicada. Taxa positiva: longs pagam shorts. O painel à direita mostra a próxima taxa e quando ela será cobrada.'
		},
		{
			art: 'trend',
			title: 'Tendência e recuos',
			body: 'As médias (SMA20, EMA50) mostram a direção; o ADX acima de 25 indica tendência. Numa tendência, comprar o recuo até a média costuma ser melhor que perseguir o topo.'
		},
		{
			art: 'leverage',
			title: 'Distância da liquidação',
			body: 'Aqui a alavancagem vai até 10×. A linha LIQ? mostra onde a posição seria liquidada antes de você enviar a ordem. Mantenha-a a mais de 10% do preço.'
		},
		{
			art: 'funding',
			title: 'Posição lotada',
			body: 'Funding muito alto significa que um lado está lotado. A multidão pode estar certa por muito tempo, mas um aperto contra ela fica possível. É um alerta, não um sinal de entrada.'
		}
	],
	en: [
		{
			art: 'funding',
			title: 'Funding',
			body: 'On a perpetual future, every 4 bars one side pays the other at the published rate. A positive rate means longs pay shorts. The right panel shows the next rate and when it is charged.'
		},
		{
			art: 'trend',
			title: 'Trends and pullbacks',
			body: 'The averages (SMA20, EMA50) show direction; ADX above 25 signals a trend. In a trend, buying the pullback to the average usually beats chasing the high.'
		},
		{
			art: 'leverage',
			title: 'Distance to liquidation',
			body: 'Leverage goes up to 10× here. The LIQ? line shows where the position would be liquidated before you send the order. Keep it more than 10% away from price.'
		},
		{
			art: 'funding',
			title: 'Crowded trades',
			body: 'Very high funding means one side is crowded. The crowd can be right for a long time, but a squeeze against it becomes possible. It is a warning, not an entry signal.'
		}
	]
};

const m03: Cards = {
	'pt-BR': [
		{
			art: 'leverage',
			title: 'O que a alavancagem muda',
			body: 'Alavancagem não muda quanto você ganha ou perde por contrato quando o preço anda. Ela muda a margem que você deposita e o quão perto fica a liquidação. A 20×, a margem é 5% do nocional, mas a de manutenção (0,5%) precisa ficar: um movimento de cerca de 4,5% contra você já liquida.'
		},
		{
			art: 'volatility',
			title: 'Volatilidade',
			body: 'O ATR mede o tamanho típico de uma barra. Mercados quietos não ficam quietos para sempre: bandas de Bollinger apertadas costumam vir antes de um movimento grande, de direção desconhecida.'
		},
		{
			art: 'margin',
			title: 'Margem isolada',
			body: 'Cada posição tem a sua margem. "+ margem" afasta a liquidação sem mudar o tamanho. O ticket avisa quando o stop está além da liquidação: ele nunca seria executado.'
		},
		{
			art: 'risk',
			title: 'Stop não é preço garantido',
			body: 'Um stop vira ordem a mercado quando é atingido. Se o mercado abrir com gap além dele, a execução sai pior que o preço do stop. Dimensione pensando nisso.'
		}
	],
	en: [
		{
			art: 'leverage',
			title: 'What leverage changes',
			body: 'Leverage does not change how much you gain or lose per contract when price moves. It changes the margin you post and how close liquidation is. At 20×, the margin is 5% of the notional, but the maintenance margin (0.5%) must stay: a move of about 4.5% against you already liquidates.'
		},
		{
			art: 'volatility',
			title: 'Volatility',
			body: 'ATR measures the typical size of a bar. Quiet markets do not stay quiet forever: tight Bollinger bands often come before a large move, in an unknown direction.'
		},
		{
			art: 'margin',
			title: 'Isolated margin',
			body: 'Each position has its own margin. "+ margin" pushes liquidation away without changing the size. The ticket warns when the stop is beyond liquidation: it would never fill.'
		},
		{
			art: 'risk',
			title: 'A stop is not a guaranteed price',
			body: 'A stop becomes a market order when it is hit. If the market opens with a gap beyond it, the fill is worse than the stop price. Size with that in mind.'
		}
	]
};

const m04: Cards = {
	'pt-BR': [
		{
			art: 'payoff',
			title: 'Calls e puts',
			body: 'Uma call ganha se o preço terminar acima do strike; uma put, se terminar abaixo. Quem compra paga o prêmio, e esse prêmio é a perda máxima.'
		},
		{
			art: 'chain',
			title: 'A cadeia de opções',
			body: 'Na aba Opções, cada linha é um strike e cada bloco um vencimento. Compre clicando no ask, o preço pedido por quem vende: é o que você paga. A volatilidade implícita (IV) é o preço do seguro: IV alta, opção cara.'
		},
		{
			art: 'decay',
			title: 'O tempo corre contra o comprador',
			body: 'A cada barra a opção perde um pouco de valor ([[theta|theta]]). Você precisa do movimento e precisa dele a tempo. [[delta|Delta]] diz quanto a opção anda quando o preço anda 1.'
		},
		{
			art: 'risk',
			title: 'Dimensione pelo prêmio',
			body: 'O risco de uma opção comprada é o prêmio total pago. Mantenha-o dentro de 1–2% do patrimônio por ideia, como faria com um stop.'
		}
	],
	en: [
		{
			art: 'payoff',
			title: 'Calls and puts',
			body: 'A call gains if price ends above the strike; a put, if it ends below. The buyer pays the premium, and that premium is the maximum loss.'
		},
		{
			art: 'chain',
			title: 'The option chain',
			body: 'In the Options tab, each row is a strike and each block an expiry. Buy by clicking the ask, the price sellers ask: it is what you pay. Implied volatility (IV) is the price of insurance: high IV, expensive option.'
		},
		{
			art: 'decay',
			title: 'Time works against the buyer',
			body: 'Every bar the option loses a little value ([[theta|theta]]). You need the move, and you need it in time. [[delta|Delta]] says how much the option moves when price moves by 1.'
		},
		{
			art: 'risk',
			title: 'Size by the premium',
			body: 'The risk of a bought option is the total premium paid. Keep it within 1–2% of equity per idea, as you would with a stop.'
		}
	]
};

const m05: Cards = {
	'pt-BR': [
		{
			art: 'payoff',
			title: 'Vender prêmio',
			body: 'Quem vende a opção recebe o prêmio na hora. O ganho máximo é esse prêmio; a perda pode ser muitas vezes maior se o preço passar do strike.'
		},
		{
			art: 'margin',
			title: 'Margem e chamada de margem',
			body: 'Opção vendida exige margem, e ela cresce quando a opção anda contra você. Se a margem não cobre, há [[margin-call|chamada de margem]] e recompra forçada, no pior momento.'
		},
		{
			art: 'chain',
			title: 'IV alta, prêmio alto',
			body: 'IV alta paga mais prêmio porque o mercado espera movimentos maiores. Venda pelo bid, o preço oferecido por quem compra: é o que você recebe. O prêmio recebido é pagamento por um risco, não lucro garantido.'
		},
		{
			art: 'risk',
			title: 'Poucos contratos',
			body: 'Pense no pior movimento plausível, não no prêmio. Venda poucos contratos e saiba antes em que ponto você recompra.'
		}
	],
	en: [
		{
			art: 'payoff',
			title: 'Selling premium',
			body: 'The option seller receives the premium up front. The maximum gain is that premium; the loss can be many times larger if price moves past the strike.'
		},
		{
			art: 'margin',
			title: 'Margin and margin calls',
			body: 'A sold option needs margin, and it grows when the option moves against you. If the margin does not cover it, there is a [[margin-call|margin call]] and a forced buyback, at the worst moment.'
		},
		{
			art: 'chain',
			title: 'High IV, high premium',
			body: 'High IV pays more premium because the market expects bigger moves. Sell at the bid, the price buyers offer: it is what you receive. The premium received is payment for a risk, not a guaranteed profit.'
		},
		{
			art: 'risk',
			title: 'Few contracts',
			body: 'Think about the worst plausible move, not the premium. Sell few contracts and know in advance at which point you buy back.'
		}
	]
};

// The campaign's first mission (design D8.4): it carries the introduction (chart; plan, advisor and
// points). m01 keeps its own cards for free play.
const m06: Cards = {
	'pt-BR': [
		m01['pt-BR'][0],
		{
			art: 'book',
			title: 'Livro fino',
			body: 'Aqui há poucas ofertas no livro: o spread é largo e cada ordem a mercado paga spread e slippage. Por barra só dá para negociar 4% do volume; uma ordem maior executa em partes. O debrief mostra quanto spread e slippage você pagou.'
		},
		{
			art: 'ticket',
			title: 'Mercado, limite e alvo',
			body: 'Nesta missão só é possível comprar (long), com até 2×. Ordem a mercado executa na abertura da próxima barra e paga o spread; limite espera o seu preço, paga só a taxa de maker e pode não executar. Aceita não significa executada: o feed avisa quando executar.'
		},
		{
			art: 'risk',
			title: 'O stop vem primeiro',
			body: 'Defina o stop antes da entrada e dimensione por risco: 1% do patrimônio em jogo por ideia. Um stop vira ordem a mercado quando é atingido, e num livro fino ele executa além do preço do stop.'
		},
		m01['pt-BR'][4]
	],
	en: [
		m01.en[0],
		{
			art: 'book',
			title: 'Thin book',
			body: 'Few quotes rest in the book here: the spread is wide and every market order pays spread and slippage. Each bar you can trade only 4% of its volume; a larger order fills in pieces. The debrief shows how much spread and slippage you paid.'
		},
		{
			art: 'ticket',
			title: 'Market, limit and target',
			body: 'In this mission you can only buy (long), at up to 2×. A market order fills at the next bar’s open and pays the spread; a limit waits for your price, pays only the maker fee and may not fill. Accepted is not filled: the feed tells you when it fills.'
		},
		{
			art: 'risk',
			title: 'The stop comes first',
			body: 'Set the stop before the entry and size by risk: 1% of equity at stake per idea. A stop becomes a market order when it is hit, and in a thin book it fills beyond the stop price.'
		},
		m01.en[4]
	]
};

const m07: Cards = {
	'pt-BR': [
		{
			art: 'risk',
			title: 'Tamanho pelo stop',
			body: 'Quantidade = risco em dinheiro ÷ distância até o stop. A alavancagem muda só a margem, não o risco: é o stop que decide quanto você perde. Ponha o stop além da estrutura (um fundo ou um topo), não num número redondo.'
		},
		{
			art: 'trail',
			title: 'Stop móvel',
			body: 'O stop móvel segue o melhor fechamento a uma distância fixa e nunca recua. Ele é recalculado no fechamento e passa a valer na barra seguinte. Perto demais do preço, recuos normais o atingem: compare a distância com o ATR.'
		},
		{
			art: 'trend',
			title: 'Tendência e recuos',
			body: 'As médias (SMA20, EMA50) mostram a direção; o ADX acima de 25 indica tendência. Numa tendência, recuos até a média são comuns e não mudam a direção por si só.'
		},
		{
			art: 'plan',
			title: 'Múltiplos de R',
			body: 'R é o risco planejado de um trade: ganhar 2R é ganhar o dobro do que você arriscou. Um alvo fixo costuma acertar mais vezes; um stop móvel acerta menos, mas pode ficar mais tempo no movimento. Escolha a saída antes de entrar e registre no plano.'
		}
	],
	en: [
		{
			art: 'risk',
			title: 'Size from the stop',
			body: 'Quantity = money at risk ÷ distance to the stop. Leverage changes only the margin, not the risk: the stop decides how much you lose. Put the stop beyond structure (a low or a high), not at a round number.'
		},
		{
			art: 'trail',
			title: 'Trailing stop',
			body: 'A trailing stop follows the best close at a fixed distance and never moves back. It is recomputed at the close and takes effect from the next bar. Too close to price, ordinary pullbacks hit it: compare the distance with the ATR.'
		},
		{
			art: 'trend',
			title: 'Trends and pullbacks',
			body: 'The averages (SMA20, EMA50) show direction; ADX above 25 signals a trend. In a trend, pullbacks to the average are common and do not change the direction by themselves.'
		},
		{
			art: 'plan',
			title: 'R multiples',
			body: 'R is a trade’s planned risk: making 2R means making twice what you risked. A fixed target usually wins more often; a trailing stop wins less often but can stay longer in the move. Choose the exit before entering and record it in the plan.'
		}
	]
};

const m08: Cards = {
	'pt-BR': [
		{
			art: 'trend',
			title: 'Tendência ou lateral?',
			body: 'Numa lateral o preço volta para o meio; numa tendência ele se afasta dele. Comprar o fundo e vender o topo funciona numa e perde na outra; seguir o movimento é o contrário.'
		},
		{
			art: 'regime',
			title: 'Filtro de regime',
			body: 'ADX acima de 25 e bandas de Bollinger se abrindo sugerem tendência; ADX abaixo de 20 e bandas estreitas, lateral. Os dois são calculados com barras passadas e mudam tarde, depois que o mercado já mudou.'
		},
		{
			art: 'volatility',
			title: 'Aperto e expansão',
			body: 'Bandas estreitas mostram volatilidade comprimida, e a expansão costuma vir depois. A direção da expansão não está no indicador.'
		},
		{
			art: 'chart',
			title: 'Nenhum indicador sozinho',
			body: 'Antes de cada entrada, escreva no plano em que regime você acha que está e por quê. Use uma ferramenta por regime e compare no debrief quantas vezes cada leitura acertou.'
		}
	],
	en: [
		{
			art: 'trend',
			title: 'Trend or range?',
			body: 'In a range price returns to the middle; in a trend it moves away from it. Buying the low and selling the high works in one and loses in the other; following the move is the opposite.'
		},
		{
			art: 'regime',
			title: 'A regime filter',
			body: 'ADX above 25 and widening Bollinger bands suggest a trend; ADX below 20 and narrow bands, a range. Both are computed from past bars and turn late, after the market has already changed.'
		},
		{
			art: 'volatility',
			title: 'Squeeze and expansion',
			body: 'Narrow bands show compressed volatility, and expansion tends to follow. The direction of the expansion is not in the indicator.'
		},
		{
			art: 'chart',
			title: 'No indicator alone',
			body: 'Before each entry, write in the plan which regime you think you are in, and why. Use one tool per regime and compare in the debrief how often each read was right.'
		}
	]
};

const m09: Cards = {
	'pt-BR': [
		{
			art: 'calendar',
			title: 'Agenda anunciada',
			body: 'Algumas divulgações são anunciadas com antecedência: o painel de notícias mostra a barra e o consenso, nunca o resultado. O consenso já está no preço; só a surpresa (anunciado − consenso) move o valor justo.'
		},
		{
			art: 'straddle',
			title: 'Straddle: dois pontos de equilíbrio',
			body: 'Comprar uma call e uma put do mesmo strike e vencimento ganha com um movimento grande para qualquer lado. Os pontos de equilíbrio ficam no strike mais e menos o prêmio total pago. Entre eles, o trade perde, no máximo o prêmio.'
		},
		{
			art: 'volatility',
			title: 'Implícita × realizada',
			body: 'O prêmio embute o movimento que o mercado espera (a volatilidade implícita, IV). O straddle só paga se o movimento realizado superar esse movimento implícito. Antes de um evento a IV costuma subir, e o prêmio fica caro.'
		},
		{
			art: 'decay',
			title: 'Theta e a queda da IV',
			body: 'Cada barra de espera custa theta ao comprador. Depois de uma divulgação, a incerteza sai do preço e a IV costuma cair, levando parte do prêmio junto. Comprar depois da notícia paga o prêmio e já perdeu o gap.'
		},
		{
			art: 'chain',
			title: 'Strike e vencimento',
			body: 'Escolha um vencimento que passe da divulgação, ou a opção vence antes do evento. Strikes perto do preço (ATM) reagem mais ao movimento; os distantes custam menos e pagam só com movimentos maiores. Nesta missão você só compra opções.'
		}
	],
	en: [
		{
			art: 'calendar',
			title: 'The announced calendar',
			body: 'Some releases are announced ahead: the news panel shows their bar and consensus, never the outcome. The consensus is already in the price; only the surprise (announced − consensus) moves fair value.'
		},
		{
			art: 'straddle',
			title: 'Straddle: two breakevens',
			body: 'Buying a call and a put of the same strike and expiry gains on a large move either way. The breakevens sit at the strike plus and minus the total premium paid. Between them the trade loses, at most the premium.'
		},
		{
			art: 'volatility',
			title: 'Implied vs realized',
			body: 'The premium prices the move the market expects (implied volatility, IV). A straddle pays only if the realized move beats that implied move. Before an event IV tends to rise, and the premium gets expensive.'
		},
		{
			art: 'decay',
			title: 'Theta and falling IV',
			body: 'Every bar of waiting costs the buyer theta. After a release the uncertainty leaves the price and IV tends to fall, taking part of the premium with it. Buying after the news pays the premium and has already missed the gap.'
		},
		{
			art: 'chain',
			title: 'Strike and expiry',
			body: 'Choose an expiry after the release, or the option expires before the event. Strikes near the price (ATM) respond more to the move; distant ones cost less and pay only on bigger moves. In this mission you can only buy options.'
		}
	]
};

const m10: Cards = {
	'pt-BR': [
		{
			art: 'payoff',
			title: 'Put de proteção e collar',
			body: 'Comprado no perpétuo, uma put compra um piso: abaixo do strike, a put ganha o que a posição perde. O collar vende uma call acima do preço para pagar a put, abrindo mão da alta além dela.'
		},
		{
			art: 'margin',
			title: 'Cada perna tem sua margem',
			body: 'Aqui cada opção vendida tem margem própria: o ganho do perpétuo não libera margem da call vendida. As opções compradas contam no patrimônio pelo bid. Abaixo da margem de manutenção há recompra forçada.'
		},
		{
			art: 'funding',
			title: 'Carrego: funding × theta',
			body: 'Ficar comprado no perpétuo custa funding quando muitos estão comprados. Uma call comprada custa theta em vez disso. Compare os dois antes de escolher como ficar exposto.'
		},
		{
			art: 'chain',
			title: 'O prazo da proteção',
			body: 'Seguro tem prazo: uma put vence numa barra certa, e o risco pode chegar depois dela. Prazos mais longos custam mais e protegem por mais tempo.'
		},
		{
			art: 'risk',
			title: 'Perda máxima de cada estrutura',
			body: 'Put de proteção: entrada menos strike, mais o prêmio. Spread comprado: o prêmio líquido. Opção vendida a descoberto: muito mais do que o prêmio recebido.'
		}
	],
	en: [
		{
			art: 'payoff',
			title: 'Protective put and collar',
			body: 'Long the perpetual, a put buys a floor: below the strike, the put gains what the position loses. A collar sells a call above the price to pay for the put, giving up the upside beyond it.'
		},
		{
			art: 'margin',
			title: 'Each leg has its own margin',
			body: 'Here each written option is margined on its own: the perpetual’s gain does not free the written call’s margin. Bought options count toward equity at the bid. Below maintenance margin there is a forced buy-back.'
		},
		{
			art: 'funding',
			title: 'Carry: funding vs theta',
			body: 'Holding the perpetual long costs funding when many traders are long. A bought call costs theta instead. Compare the two before choosing how to be exposed.'
		},
		{
			art: 'chain',
			title: 'The tenor of the hedge',
			body: 'Insurance has a tenor: a put expires on a set bar, and the risk can come after it. Longer tenors cost more and protect for longer.'
		},
		{
			art: 'risk',
			title: 'Each structure’s maximum loss',
			body: 'Protective put: entry minus strike, plus the premium. A bought spread: the net premium. A naked written option: far more than the premium collected.'
		}
	]
};

const m11: Cards = {
	'pt-BR': [
		{
			art: 'calendar',
			title: 'A agenda',
			body: 'O painel de notícias lista as divulgações anunciadas: o que é, em que barra sai e o consenso. O resultado só aparece na divulgação.'
		},
		{
			art: 'news',
			title: 'Expectativa × surpresa',
			body: 'O consenso já está no preço: um resultado igual a ele não muda o valor justo. A surpresa, pesada pela credibilidade, muda, e quase toda entra no gap da abertura seguinte. Notícia boa pode cair, se o mercado já esperava mais.'
		},
		{
			art: 'risk',
			title: 'Gaps saltam stops',
			body: 'Um stop vira ordem a mercado quando é atingido; num gap, ele executa no preço da abertura, não no seu. Conte o gap no risco de quem atravessa uma divulgação posicionado.'
		},
		{
			art: 'plan',
			title: 'Decidir antes',
			body: 'Antes de cada divulgação, escolha: fora, pequeno ou protegido, e registre o motivo no plano. Ficar fora também é uma decisão.'
		},
		{
			art: 'advisor',
			title: 'Por que o assessor espera',
			body: 'Com uma divulgação anunciada a até 3 barras, o assessor puxa a leitura para esperar. Ele não sabe o resultado, assim como você.'
		}
	],
	en: [
		{
			art: 'calendar',
			title: 'The calendar',
			body: 'The news panel lists announced releases: what each one is, the bar it comes out and its consensus. The outcome appears only at the release.'
		},
		{
			art: 'news',
			title: 'Expectation vs surprise',
			body: 'The consensus is already in the price: an outcome equal to it does not change fair value. The surprise, weighted by credibility, does, and almost all of it lands in the next open’s gap. Good news can sell off when the market expected more.'
		},
		{
			art: 'risk',
			title: 'Gaps jump stops',
			body: 'A stop becomes a market order when it is hit; in a gap it fills at the open, not at your price. Count the gap in the risk of holding a position through a release.'
		},
		{
			art: 'plan',
			title: 'Decide before',
			body: 'Before each release, choose: flat, small or hedged, and record why in the plan. Staying flat is a decision too.'
		},
		{
			art: 'advisor',
			title: 'Why the advisor waits',
			body: 'With an announced release up to 3 bars away, the advisor pulls its read toward wait. It does not know the outcome, and neither do you.'
		}
	]
};

const m12: Cards = {
	'pt-BR': [
		{
			art: 'report',
			title: 'Expectativa e amostra',
			body: 'Expectativa é o resultado médio por trade em R: taxa de acerto vezes ganho médio, menos taxa de erro vezes perda média. Com poucos trades ela é quase só sorte. Na campanha, o debrief desta missão traz o relatório de todas as missões.'
		},
		{
			art: 'regime',
			title: 'Primeiro o regime',
			body: 'Antes da ferramenta, identifique o regime: lateral, tendência, volatilidade alta ou livro fino. Cada ferramenta serve a um regime e perde nos outros.'
		},
		{
			art: 'calendar',
			title: 'A divulgação',
			body: 'Há divulgação anunciada nesta missão: veja a barra e o consenso no painel de notícias. Decida antes dela como quer estar posicionado.'
		},
		{
			art: 'funding',
			title: 'Receber para carregar',
			body: 'Quando os shorts estão lotados, quem fica comprado recebe funding a cada intervalo. O ganho é pequeno e certo; a perda, se o preço cair, não é.'
		},
		{
			art: 'risk',
			title: 'Um trade não pode acabar com a campanha',
			body: 'Dimensione pelo pior caso, contando gaps e saltos. Sem liquidação e sem chamada de margem: o capital que sobra é o resultado final da campanha.'
		}
	],
	en: [
		{
			art: 'report',
			title: 'Expectancy and sample size',
			body: 'Expectancy is the average result per trade in R: win rate times average win, minus loss rate times average loss. With few trades it is mostly luck. In a campaign, this mission’s debrief carries the report of the whole run.'
		},
		{
			art: 'regime',
			title: 'The regime first',
			body: 'Before the tool, identify the regime: range, trend, high volatility or a thin book. Each tool fits one regime and loses in the others.'
		},
		{
			art: 'calendar',
			title: 'The release',
			body: 'There is an announced release in this mission: see its bar and consensus in the news panel. Decide before it how you want to be positioned.'
		},
		{
			art: 'funding',
			title: 'Paid to carry',
			body: 'When shorts are crowded, longs collect funding every interval. The gain is small and steady; the loss, if price falls, is not.'
		},
		{
			art: 'risk',
			title: 'One trade must not end the campaign',
			body: 'Size for the worst case, counting gaps and jumps. No liquidation and no margin call: what is left is the campaign’s final result.'
		}
	]
};

// Strategies (add-academy-and-strategies design D8). Every mission that writes options states its margin
// rule (spec mission-tutorial "Strategy tutorials state the margin rule"); none says what the seed drew.
const m13: Cards = {
	'pt-BR': [
		{
			art: 'spread',
			title: 'Trava de alta',
			body: 'Compre uma call e venda outra de strike maior, no mesmo vencimento. O prêmio da vendida paga parte da comprada: você arrisca menos, mas o ganho para no strike de cima. Lucro máximo = largura − débito; perda máxima = o débito.'
		},
		{
			art: 'payoff',
			title: 'Dois pontos de equilíbrio',
			body: 'A call sozinha empata em strike + prêmio; a trava, em strike de baixo + débito, mais perto do preço. Calcule os dois antes de entrar, com os prêmios da cadeia. Num movimento modesto a trava rende mais pelo que arrisca; num movimento grande, a call sozinha.'
		},
		{
			art: 'volatility',
			title: 'Opções caras',
			body: 'Quando a volatilidade implícita está alta, toda opção está cara. Na trava você compra uma opção cara e vende outra também cara: delta, vega e theta ficam menores que os da call sozinha. Compare a IV da cadeia com o tamanho das barras recentes.'
		},
		{
			art: 'margin',
			title: 'Margem da trava',
			body: 'Nesta missão, uma opção vendida coberta por uma comprada do mesmo tipo e vencimento pede margem só do que o par ainda pode perder. A trava de alta comprada por um débito não pede margem; uma call vendida sozinha pede a margem inteira. Feche a vendida antes ou junto com a comprada.'
		}
	],
	en: [
		{
			art: 'spread',
			title: 'Bull call spread',
			body: 'Buy a call and sell another with a higher strike, same expiry. The written call’s premium pays for part of the bought one: you risk less, but the gain stops at the upper strike. Maximum profit = width − debit; maximum loss = the debit.'
		},
		{
			art: 'payoff',
			title: 'Two breakevens',
			body: 'The call alone breaks even at strike + premium; the spread at the lower strike + debit, closer to the price. Work out both before entering, with the chain’s premiums. In a modest move the spread earns more for what it risks; in a large move, the call alone does.'
		},
		{
			art: 'volatility',
			title: 'Expensive options',
			body: 'When implied volatility is high, every option is expensive. In the spread you buy an expensive option and sell another expensive one: delta, vega and theta are smaller than the call’s alone. Compare the chain’s IV with the size of recent bars.'
		},
		{
			art: 'margin',
			title: 'The spread’s margin',
			body: 'In this mission, a written option covered by a bought one of the same kind and expiry needs margin only for what the pair can still lose. A bull call spread bought for a debit needs no margin; a call written alone needs the full margin. Close the written leg before or with the bought one.'
		}
	]
};

const m14: Cards = {
	'pt-BR': [
		{
			art: 'covered',
			title: 'Venda coberta',
			body: 'Comprado no perpétuo, venda uma call acima do preço e receba o prêmio. O ponto de equilíbrio cai pelo prêmio, mas o ganho para no strike: lucro máximo = strike − entrada + prêmio. O desenho é o mesmo de uma put vendida.'
		},
		{
			art: 'payoff',
			title: 'O que o prêmio cobre',
			body: 'Numa queda, o prêmio amortece só o valor dele: o long continua perdendo. Escreva o lucro máximo antes de vender a call. Cada vencimento é uma decisão nova: vender outra call ou não.'
		},
		{
			art: 'margin',
			title: 'Coberta no resultado, não na margem',
			body: 'Aqui a call vendida tem margem própria mesmo com o long cobrindo o resultado, porque o perpétuo tem margem separada. Só um par de opções do mesmo tipo e vencimento pede menos margem, e a venda coberta não é um par de opções. Deixe folga para as duas margens.'
		},
		{
			art: 'funding',
			title: 'Prêmio contra funding',
			body: 'O long no perpétuo paga ou recebe funding a cada intervalo. Compare o prêmio que você recebe com o funding que você paga. Os vencimentos vêm a cada 24 barras.'
		}
	],
	en: [
		{
			art: 'covered',
			title: 'Covered call',
			body: 'Long the perpetual, sell a call above the price and collect the premium. The breakeven drops by the premium, but the gain stops at the strike: maximum profit = strike − entry + premium. The shape is the same as a written put.'
		},
		{
			art: 'payoff',
			title: 'What the premium covers',
			body: 'In a fall, the premium cushions only its own amount: the long keeps losing. Write the maximum profit before selling the call. Each expiry is a new decision: sell another call or not.'
		},
		{
			art: 'margin',
			title: 'Covered in P&L, not in margin',
			body: 'Here the written call needs its own margin even while the long covers it in P&L, because the perpetual is margined separately. Only a pair of options of the same kind and expiry needs less margin, and a covered call is not a pair of options. Keep a buffer for both margins.'
		},
		{
			art: 'funding',
			title: 'Premium against funding',
			body: 'The perpetual long pays or earns funding at each interval. Compare the premium you collect with the funding you pay. Expiries come every 24 bars.'
		}
	]
};

const m15: Cards = {
	'pt-BR': [
		{
			art: 'insurance',
			title: 'Put de proteção',
			body: 'Comprado no perpétuo, uma put compra um piso: abaixo do strike, a put ganha o que a posição perde. Perda máxima = entrada − strike + prêmio + custos. Escreva o piso antes de comprar.'
		},
		{
			art: 'risk',
			title: 'Stop ou seguro',
			body: 'Um stop não custa nada antes, mas executa no próximo preço disponível: depois de um gap, bem além do gatilho. A put custa o prêmio e garante o strike até o vencimento. Uma queda forte pode vir ou não; a escolha é quanto pagar para não depender do gap.'
		},
		{
			art: 'chain',
			title: 'Strike e prazo',
			body: 'O strike é a franquia: uma put mais abaixo custa menos e põe o piso mais baixo. O prazo precisa cobrir a janela de risco: uma put que vence antes não protege depois. Nesta missão você só compra opções.'
		},
		{
			art: 'decay',
			title: 'O custo de esperar',
			body: 'Enquanto nada acontece, a put perde valor de tempo a cada barra (theta). Na maior parte do tempo um seguro não é usado: o prêmio é o preço da proteção. Compare esse custo com a perda que ele evita.'
		}
	],
	en: [
		{
			art: 'insurance',
			title: 'Protective put',
			body: 'Long the perpetual, a put buys a floor: below the strike, the put gains what the position loses. Maximum loss = entry − strike + premium + costs. Write the floor before buying.'
		},
		{
			art: 'risk',
			title: 'Stop or insurance',
			body: 'A stop costs nothing up front, but it fills at the next available price: after a gap, far beyond its trigger. The put costs its premium and guarantees the strike until expiry. A sharp fall may or may not come; the choice is how much to pay so as not to depend on the gap.'
		},
		{
			art: 'chain',
			title: 'Strike and tenor',
			body: 'The strike is the deductible: a lower put costs less and sets a lower floor. The tenor must cover the risk window: a put that expires first protects nothing after. In this mission you only buy options.'
		},
		{
			art: 'decay',
			title: 'The cost of waiting',
			body: 'While nothing happens, the put loses time value every bar (theta). Most of the time insurance goes unused: the premium is the price of protection. Compare that cost with the loss it prevents.'
		}
	]
};

const m16: Cards = {
	'pt-BR': [
		{
			art: 'condor',
			title: 'Iron condor e strangle',
			body: 'Vender uma put e uma call fora do dinheiro (strangle) recebe prêmio, com perda aberta além dos pontos de equilíbrio. O iron condor compra asas mais longe: recebe menos, mas a perda máxima é largura − crédito. Risco definido contra risco aberto.'
		},
		{
			art: 'volatility',
			title: 'Implícita e realizada',
			body: 'Quem vende prêmio ganha quando a volatilidade implícita paga mais do que o mercado realiza. Compare a IV da cadeia com o tamanho das barras recentes. Vender prêmio ganha pouco muitas vezes e às vezes perde muito.'
		},
		{
			art: 'margin',
			title: 'Margem por perna e margem do par',
			body: 'Aqui cada vendida coberta por uma comprada do mesmo tipo e vencimento pede só o que o par pode perder: o condor pede a largura. O strangle não tem asas: cada perna tem margem própria, que cresce quando o preço se aproxima do strike. Abaixo da manutenção há recompra forçada.'
		},
		{
			art: 'risk',
			title: 'Dimensione pelo pior caso',
			body: 'Numa estrutura de risco definido, a perda máxima é conhecida: arrisque até 2% do patrimônio nela. Escreva antes o que fará se o preço romper o range. Quatro pernas pagam quatro spreads.'
		},
		{
			art: 'decay',
			title: 'Theta e gama',
			body: 'Theta trabalha para quem vende enquanto o preço fica longe dos strikes. Perto de um strike vendido o gama cresce: o delta muda rápido e o prejuízo acelera. Theta paga o tempo; gama cobra o movimento.'
		}
	],
	en: [
		{
			art: 'condor',
			title: 'Iron condor and strangle',
			body: 'Selling an out-of-the-money put and call (a strangle) collects premium, with an open loss beyond its breakevens. An iron condor buys wings further out: it collects less, but its maximum loss is width − credit. Defined against open risk.'
		},
		{
			art: 'volatility',
			title: 'Implied and realized',
			body: 'Premium sellers win when implied volatility pays more than the market realizes. Compare the chain’s IV with the size of recent bars. Selling premium wins a little often and sometimes loses a lot.'
		},
		{
			art: 'margin',
			title: 'Per-leg margin and pair margin',
			body: 'Here each written option covered by a bought one of the same kind and expiry needs only what the pair can lose: the condor needs its width. The strangle has no wings: each leg is margined on its own, and its margin grows as the price nears the strike. Below maintenance there is a forced buy-back.'
		},
		{
			art: 'risk',
			title: 'Size by the worst case',
			body: 'In a defined-risk structure the maximum loss is known: risk at most 2% of equity on it. Write beforehand what you will do if the price breaks out of the range. Four legs pay four spreads.'
		},
		{
			art: 'decay',
			title: 'Theta and gamma',
			body: 'Theta works for the seller while the price stays away from the strikes. Near a written strike gamma grows: delta changes fast and the loss accelerates. Theta pays for time; gamma charges for movement.'
		}
	]
};

const m17: Cards = {
	'pt-BR': [
		{
			art: 'roll',
			title: 'Rolar',
			body: 'Rolar é fechar a opção do vencimento próximo e abrir outra num vencimento seguinte: duas operações, dois spreads. Julgue a rolagem como uma entrada nova. Rolar uma posição perdedora só para esperar que ela volte soma tempo e custo.'
		},
		{
			art: 'decay',
			title: 'O relógio acelera',
			body: 'O theta por barra de uma opção no dinheiro cresce perto do vencimento, e o gama também: nas últimas barras um movimento pequeno leva o delta de perto de 0 a perto de 1. O prêmio que falta ganhar fica pequeno diante do que uma barra pode mexer.'
		},
		{
			art: 'chain',
			title: 'O próximo vencimento',
			body: 'Os vencimentos vêm a cada 25 barras: sempre há um próximo e um seguinte. Um prazo mais longo custa mais e tem theta menor por barra. Escolha o vencimento pela sua tese, não por hábito.'
		},
		{
			art: 'plan',
			title: 'Decida antes',
			body: 'Antes das 5 últimas barras de um vencimento, decida fechar, rolar ou segurar até a liquidação, e registre por quê. Sem tese nova, não role. Na margem, cada vendida coberta por uma comprada do mesmo tipo e vencimento pede só o que o par pode perder.'
		}
	],
	en: [
		{
			art: 'roll',
			title: 'Rolling',
			body: 'Rolling is closing the near expiry’s option and opening another in a later expiry: two trades, two spreads. Judge the roll as a fresh entry. Rolling a loser just to wait for it to come back adds time and cost.'
		},
		{
			art: 'decay',
			title: 'The clock speeds up',
			body: 'An at-the-money option’s theta per bar grows near expiry, and so does its gamma: in the last bars a small move swings delta from near 0 to near 1. The premium left to earn becomes small against what one bar can move.'
		},
		{
			art: 'chain',
			title: 'The next expiry',
			body: 'Expiries come every 25 bars: there is always a near one and a later one. A longer tenor costs more and has a smaller theta per bar. Choose the expiry by your thesis, not by habit.'
		},
		{
			art: 'plan',
			title: 'Decide beforehand',
			body: 'Before an expiry’s last 5 bars, decide to close, roll or hold to settlement, and record why. Without a new thesis, do not roll. For margin, each written option covered by a bought one of the same kind and expiry needs only what the pair can lose.'
		}
	]
};

const TUTORIALS: Record<string, Cards> = {
	'm01-false-breakout': m01,
	'm02-funding-squeeze': m02,
	'm03-volatility-shock': m03,
	'm04-options-buyer': m04,
	'm05-options-writer': m05,
	'm06-thin-book': m06,
	'm07-trend-trailing': m07,
	'm08-regime-switch': m08,
	'm09-event-straddle': m09,
	'm10-hedged-book': m10,
	'm11-news-calendar': m11,
	'm12-capstone': m12,
	'm13-call-spread': m13,
	'm14-covered-call': m14,
	'm15-protective-put': m15,
	'm16-range-condor': m16,
	'm17-before-expiry': m17
};

export function tutorialFor(missionId: string, locale: Locale): TutorialCard[] {
	return TUTORIALS[missionId]?.[locale] ?? [];
}

export const TUTORIAL_MISSIONS = Object.keys(TUTORIALS);
