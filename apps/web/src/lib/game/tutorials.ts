// Pre-mission tutorial cards (spec mission-tutorial "Tutorial before a mission").
// They teach the concept, what to watch and which controls to use. Like the mission titles, they
// must not reveal what the market will do: what a scenario was built to teach stays in the debrief.

import type { Locale } from './i18n.svelte';

export type TutorialArt = 'chart' | 'ticket' | 'risk' | 'news' | 'plan' | 'funding' | 'trend' | 'leverage' | 'volatility' | 'payoff' | 'chain' | 'decay' | 'margin';

export interface TutorialCard {
	art: TutorialArt;
	title: string;
	body: string; // at most three sentences
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
			art: 'plan',
			title: 'Plano, e esperar também vale',
			body: 'Antes de cada entrada você registra a hipótese e onde ela estaria errada. Perguntas de calibração medem se a sua confiança acerta. Não operar é uma decisão válida e não é penalizada.'
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
			art: 'plan',
			title: 'Plan, and waiting counts too',
			body: 'Before every entry you record your hypothesis and where it would be wrong. Calibration questions measure whether your confidence is right. Not trading is a valid decision and is not penalized.'
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
			body: 'Alavancagem não muda quanto você ganha ou perde por contrato quando o preço anda. Ela muda a margem que você deposita e o quão perto fica a liquidação. A 20×, um movimento de cerca de 5% contra você liquida.'
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
			body: 'Leverage does not change how much you gain or lose per contract when price moves. It changes the margin you post and how close liquidation is. At 20×, a move of about 5% against you liquidates.'
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
			body: 'Na aba Opções, cada linha é um strike e cada bloco um vencimento. Compre clicando no ask (o preço de venda). A volatilidade implícita (IV) é o preço do seguro: IV alta, opção cara.'
		},
		{
			art: 'decay',
			title: 'O tempo corre contra o comprador',
			body: 'A cada barra a opção perde um pouco de valor (theta). Você precisa do movimento e precisa dele a tempo. Delta diz quanto a opção anda quando o preço anda 1.'
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
			body: 'In the Options tab, each row is a strike and each block an expiry. Buy by clicking the ask (the selling price). Implied volatility (IV) is the price of insurance: high IV, expensive option.'
		},
		{
			art: 'decay',
			title: 'Time works against the buyer',
			body: 'Every bar the option loses a little value (theta). You need the move, and you need it in time. Delta says how much the option moves when price moves by 1.'
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
			body: 'Opção vendida exige margem, e ela cresce quando a opção anda contra você. Se a margem não cobre, há chamada de margem e recompra forçada, no pior momento.'
		},
		{
			art: 'chain',
			title: 'IV alta, prêmio alto',
			body: 'IV alta paga mais prêmio porque o mercado espera movimentos maiores. Venda pelo bid (o preço de compra). O prêmio recebido é pagamento por um risco, não lucro garantido.'
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
			body: 'A sold option needs margin, and it grows when the option moves against you. If the margin does not cover it, there is a margin call and a forced buyback, at the worst moment.'
		},
		{
			art: 'chain',
			title: 'High IV, high premium',
			body: 'High IV pays more premium because the market expects bigger moves. Sell at the bid (the buying price). The premium received is payment for a risk, not a guaranteed profit.'
		},
		{
			art: 'risk',
			title: 'Few contracts',
			body: 'Think about the worst plausible move, not the premium. Sell few contracts and know in advance at which point you buy back.'
		}
	]
};

const TUTORIALS: Record<string, Cards> = {
	'm01-false-breakout': m01,
	'm02-funding-squeeze': m02,
	'm03-volatility-shock': m03,
	'm04-options-buyer': m04,
	'm05-options-writer': m05
};

export function tutorialFor(missionId: string, locale: Locale): TutorialCard[] {
	return TUTORIALS[missionId]?.[locale] ?? [];
}

export const TUTORIAL_MISSIONS = Object.keys(TUTORIALS);
