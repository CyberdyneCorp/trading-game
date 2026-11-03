// Candlestick pattern reference for the cheat sheet (spec terminal-ui "Candle pattern cheat sheet").
// Shapes are schematic OHLC on a 0..100 scale; `context` draws the trend that usually precedes it.

import type { Locale } from './i18n.svelte.ts';

export type PatternBias = 'bullish' | 'bearish' | 'neutral';
export type Candle = [open: number, high: number, low: number, close: number];

interface Text {
	name: string;
	what: string; // how it looks
	means: string; // what it suggests
	confirm: string; // what to wait for
}

export interface CandlePattern {
	id: string;
	bias: PatternBias;
	context: 'up' | 'down' | null;
	candles: Candle[];
	text: Record<Locale, Text>;
}

export const CANDLE_PATTERNS: CandlePattern[] = [
	{
		id: 'hammer',
		bias: 'bullish',
		context: 'down',
		candles: [[40, 46, 8, 45]],
		text: {
			'pt-BR': {
				name: 'Martelo',
				what: 'Corpo pequeno no topo, sombra inferior ≥ 2× o corpo, quase sem sombra superior.',
				means: 'Vendedores empurraram o preço para baixo, mas compradores recuperaram até o fechamento.',
				confirm: 'Próximo candle fechando acima do martelo; melhor ainda num suporte e com volume.'
			},
			en: {
				name: 'Hammer',
				what: 'Small body at the top, lower shadow ≥ 2× the body, little or no upper shadow.',
				means: 'Sellers pushed price down but buyers took it back by the close.',
				confirm: 'Next candle closing above the hammer; stronger at support and on volume.'
			}
		}
	},
	{
		id: 'inverted-hammer',
		bias: 'bullish',
		context: 'down',
		candles: [[20, 58, 18, 26]],
		text: {
			'pt-BR': {
				name: 'Martelo invertido',
				what: 'Corpo pequeno embaixo, sombra superior longa, após queda.',
				means: 'Compradores testaram preços mais altos; a pressão vendedora pode estar acabando.',
				confirm: 'Exige confirmação: candle seguinte de alta fechando acima do corpo.'
			},
			en: {
				name: 'Inverted hammer',
				what: 'Small body at the bottom, long upper shadow, after a decline.',
				means: 'Buyers probed higher prices; selling pressure may be fading.',
				confirm: 'Needs confirmation: a bullish next candle closing above the body.'
			}
		}
	},
	{
		id: 'bullish-engulfing',
		bias: 'bullish',
		context: 'down',
		candles: [
			[42, 44, 30, 32],
			[29, 52, 27, 50]
		],
		text: {
			'pt-BR': {
				name: 'Engolfo de alta',
				what: 'Candle de alta cujo corpo cobre inteiramente o corpo de baixa anterior.',
				means: 'Os compradores assumiram o controle de forma decisiva.',
				confirm: 'Mais forte após queda longa, em suporte e com volume acima da média.'
			},
			en: {
				name: 'Bullish engulfing',
				what: 'A bullish candle whose body fully covers the previous bearish body.',
				means: 'Buyers took control decisively.',
				confirm: 'Stronger after a long decline, at support, on above-average volume.'
			}
		}
	},
	{
		id: 'piercing-line',
		bias: 'bullish',
		context: 'down',
		candles: [
			[50, 52, 30, 32],
			[26, 46, 24, 44]
		],
		text: {
			'pt-BR': {
				name: 'Linha de perfuração',
				what: 'Abre abaixo da mínima anterior e fecha acima da metade do corpo de baixa anterior.',
				means: 'A abertura fraca foi rejeitada com força.',
				confirm: 'Candle seguinte mantendo-se acima da metade do corpo de baixa.'
			},
			en: {
				name: 'Piercing line',
				what: 'Opens below the prior low and closes above the midpoint of the prior bearish body.',
				means: 'The weak open was rejected forcefully.',
				confirm: 'The next candle holding above the bearish body midpoint.'
			}
		}
	},
	{
		id: 'bullish-harami',
		bias: 'bullish',
		context: 'down',
		candles: [
			[55, 57, 22, 25],
			[33, 44, 31, 42]
		],
		text: {
			'pt-BR': {
				name: 'Harami de alta',
				what: 'Candle pequeno de alta contido no corpo grande de baixa anterior.',
				means: 'A queda perdeu força; indecisão após pressão vendedora.',
				confirm: 'Sinal fraco sozinho: espere rompimento da máxima do harami.'
			},
			en: {
				name: 'Bullish harami',
				what: 'A small bullish candle inside the previous large bearish body.',
				means: 'The decline lost momentum; indecision after selling pressure.',
				confirm: 'Weak on its own: wait for a break above the harami high.'
			}
		}
	},
	{
		id: 'tweezer-bottom',
		bias: 'bullish',
		context: 'down',
		candles: [
			[45, 47, 20, 28],
			[28, 46, 20, 42]
		],
		text: {
			'pt-BR': {
				name: 'Fundo duplo em pinça',
				what: 'Dois candles com mínimas praticamente iguais, o segundo de alta.',
				means: 'O mesmo nível segurou duas vezes: suporte de curto prazo.',
				confirm: 'Stop logo abaixo das mínimas iguais; confirme com o candle seguinte.'
			},
			en: {
				name: 'Tweezer bottom',
				what: 'Two candles with almost equal lows, the second bullish.',
				means: 'The same level held twice: short-term support.',
				confirm: 'Stop just below the matching lows; confirm with the next candle.'
			}
		}
	},
	{
		id: 'morning-star',
		bias: 'bullish',
		context: 'down',
		candles: [
			[60, 62, 34, 36],
			[30, 34, 24, 28],
			[33, 58, 31, 56]
		],
		text: {
			'pt-BR': {
				name: 'Estrela da manhã',
				what: 'Baixa forte, candle pequeno abaixo (indecisão), alta forte fechando além da metade do primeiro.',
				means: 'Transição de vendedores para compradores em três etapas.',
				confirm: 'Um dos padrões de reversão mais confiáveis, ainda assim use stop abaixo da estrela.'
			},
			en: {
				name: 'Morning star',
				what: 'Strong bearish candle, small candle lower (indecision), strong bullish candle closing past the first one’s midpoint.',
				means: 'A three-step handover from sellers to buyers.',
				confirm: 'Among the more reliable reversals; still place a stop below the star.'
			}
		}
	},
	{
		id: 'three-white-soldiers',
		bias: 'bullish',
		context: 'down',
		candles: [
			[22, 38, 20, 36],
			[32, 52, 30, 50],
			[46, 66, 44, 64]
		],
		text: {
			'pt-BR': {
				name: 'Três soldados brancos',
				what: 'Três candles de alta seguidos, cada um abrindo dentro do corpo anterior e fechando perto da máxima.',
				means: 'Compra sustentada; possível início de tendência de alta.',
				confirm: 'Cuidado se já estiver esticado (RSI alto): pode vir correção.'
			},
			en: {
				name: 'Three white soldiers',
				what: 'Three bullish candles in a row, each opening inside the prior body and closing near its high.',
				means: 'Sustained buying; a possible start of an uptrend.',
				confirm: 'Careful when already stretched (high RSI): a pullback may follow.'
			}
		}
	},
	{
		id: 'shooting-star',
		bias: 'bearish',
		context: 'up',
		candles: [[62, 92, 57, 58]],
		text: {
			'pt-BR': {
				name: 'Estrela cadente',
				what: 'Corpo pequeno embaixo, sombra superior ≥ 2× o corpo, após alta.',
				means: 'Compradores levaram o preço para cima, mas vendedores derrubaram até o fechamento.',
				confirm: 'Próximo candle fechando abaixo do corpo; mais forte em resistência.'
			},
			en: {
				name: 'Shooting star',
				what: 'Small body at the bottom, upper shadow ≥ 2× the body, after a rally.',
				means: 'Buyers lifted price but sellers pushed it back down by the close.',
				confirm: 'Next candle closing below the body; stronger at resistance.'
			}
		}
	},
	{
		id: 'hanging-man',
		bias: 'bearish',
		context: 'up',
		candles: [[86, 90, 54, 84]],
		text: {
			'pt-BR': {
				name: 'Enforcado',
				what: 'Mesmo formato do martelo, mas depois de uma alta.',
				means: 'Apareceu venda forte durante o candle; a alta pode estar cansada.',
				confirm: 'Só vale com confirmação: candle seguinte fechando abaixo do corpo.'
			},
			en: {
				name: 'Hanging man',
				what: 'Same shape as a hammer, but after a rally.',
				means: 'Heavy selling appeared intrabar; the rally may be tiring.',
				confirm: 'Only with confirmation: the next candle closing below the body.'
			}
		}
	},
	{
		id: 'bearish-engulfing',
		bias: 'bearish',
		context: 'up',
		candles: [
			[60, 72, 58, 70],
			[73, 75, 52, 54]
		],
		text: {
			'pt-BR': {
				name: 'Engolfo de baixa',
				what: 'Candle de baixa cujo corpo cobre inteiramente o corpo de alta anterior.',
				means: 'Os vendedores assumiram o controle de forma decisiva.',
				confirm: 'Mais forte após alta longa, em resistência e com volume.'
			},
			en: {
				name: 'Bearish engulfing',
				what: 'A bearish candle whose body fully covers the previous bullish body.',
				means: 'Sellers took control decisively.',
				confirm: 'Stronger after a long rally, at resistance, on volume.'
			}
		}
	},
	{
		id: 'dark-cloud-cover',
		bias: 'bearish',
		context: 'up',
		candles: [
			[52, 72, 50, 70],
			[76, 78, 56, 58]
		],
		text: {
			'pt-BR': {
				name: 'Nuvem negra',
				what: 'Abre acima da máxima anterior e fecha abaixo da metade do corpo de alta anterior.',
				means: 'A abertura forte foi vendida com força.',
				confirm: 'Candle seguinte mantendo-se abaixo da metade do corpo de alta.'
			},
			en: {
				name: 'Dark cloud cover',
				what: 'Opens above the prior high and closes below the midpoint of the prior bullish body.',
				means: 'The strong open was sold forcefully.',
				confirm: 'The next candle holding below the bullish body midpoint.'
			}
		}
	},
	{
		id: 'bearish-harami',
		bias: 'bearish',
		context: 'up',
		candles: [
			[45, 80, 43, 78],
			[68, 70, 57, 59]
		],
		text: {
			'pt-BR': {
				name: 'Harami de baixa',
				what: 'Candle pequeno de baixa contido no corpo grande de alta anterior.',
				means: 'A alta perdeu força; indecisão após pressão compradora.',
				confirm: 'Sinal fraco sozinho: espere perda da mínima do harami.'
			},
			en: {
				name: 'Bearish harami',
				what: 'A small bearish candle inside the previous large bullish body.',
				means: 'The rally lost momentum; indecision after buying pressure.',
				confirm: 'Weak on its own: wait for a break below the harami low.'
			}
		}
	},
	{
		id: 'tweezer-top',
		bias: 'bearish',
		context: 'up',
		candles: [
			[55, 82, 53, 74],
			[74, 82, 56, 58]
		],
		text: {
			'pt-BR': {
				name: 'Topo em pinça',
				what: 'Dois candles com máximas praticamente iguais, o segundo de baixa.',
				means: 'O mesmo nível rejeitou o preço duas vezes: resistência de curto prazo.',
				confirm: 'Stop logo acima das máximas iguais; confirme com o candle seguinte.'
			},
			en: {
				name: 'Tweezer top',
				what: 'Two candles with almost equal highs, the second bearish.',
				means: 'The same level rejected price twice: short-term resistance.',
				confirm: 'Stop just above the matching highs; confirm with the next candle.'
			}
		}
	},
	{
		id: 'evening-star',
		bias: 'bearish',
		context: 'up',
		candles: [
			[44, 70, 42, 68],
			[74, 80, 72, 76],
			[70, 72, 44, 46]
		],
		text: {
			'pt-BR': {
				name: 'Estrela da tarde',
				what: 'Alta forte, candle pequeno acima (indecisão), baixa forte fechando além da metade do primeiro.',
				means: 'Transição de compradores para vendedores em três etapas.',
				confirm: 'Reversão relativamente confiável; stop acima da estrela.'
			},
			en: {
				name: 'Evening star',
				what: 'Strong bullish candle, small candle higher (indecision), strong bearish candle closing past the first one’s midpoint.',
				means: 'A three-step handover from buyers to sellers.',
				confirm: 'A relatively reliable reversal; stop above the star.'
			}
		}
	},
	{
		id: 'three-black-crows',
		bias: 'bearish',
		context: 'up',
		candles: [
			[78, 80, 62, 64],
			[68, 70, 48, 50],
			[54, 56, 34, 36]
		],
		text: {
			'pt-BR': {
				name: 'Três corvos negros',
				what: 'Três candles de baixa seguidos, cada um abrindo dentro do corpo anterior e fechando perto da mínima.',
				means: 'Venda sustentada; possível início de tendência de baixa.',
				confirm: 'Se já estiver esticado (RSI baixo), um repique é comum.'
			},
			en: {
				name: 'Three black crows',
				what: 'Three bearish candles in a row, each opening inside the prior body and closing near its low.',
				means: 'Sustained selling; a possible start of a downtrend.',
				confirm: 'When already stretched (low RSI), a bounce is common.'
			}
		}
	},
	{
		id: 'doji',
		bias: 'neutral',
		context: null,
		candles: [[50, 72, 28, 50]],
		text: {
			'pt-BR': {
				name: 'Doji',
				what: 'Abertura e fechamento praticamente iguais, com sombras dos dois lados.',
				means: 'Equilíbrio entre compradores e vendedores: indecisão.',
				confirm: 'Após tendência longa pode anunciar pausa ou reversão; o próximo candle decide.'
			},
			en: {
				name: 'Doji',
				what: 'Open and close almost equal, with shadows on both sides.',
				means: 'Buyers and sellers in balance: indecision.',
				confirm: 'After a long trend it may signal a pause or reversal; the next candle decides.'
			}
		}
	},
	{
		id: 'spinning-top',
		bias: 'neutral',
		context: null,
		candles: [[46, 70, 30, 54]],
		text: {
			'pt-BR': {
				name: 'Pião',
				what: 'Corpo pequeno no meio, sombras parecidas em cima e embaixo.',
				means: 'Indecisão; nenhum lado dominou o período.',
				confirm: 'Sozinho não é sinal de entrada: observe o contexto e o candle seguinte.'
			},
			en: {
				name: 'Spinning top',
				what: 'Small body in the middle, similar shadows above and below.',
				means: 'Indecision; neither side dominated the period.',
				confirm: 'Not an entry signal on its own: watch the context and the next candle.'
			}
		}
	},
	{
		id: 'bullish-marubozu',
		bias: 'bullish',
		context: null,
		candles: [[22, 78, 22, 78]],
		text: {
			'pt-BR': {
				name: 'Marubozu de alta',
				what: 'Corpo grande de alta sem sombras: abriu na mínima, fechou na máxima.',
				means: 'Compradores no controle o período inteiro; força/continuação.',
				confirm: 'Rompimento com marubozu e volume tende a ter continuidade.'
			},
			en: {
				name: 'Bullish marubozu',
				what: 'Large bullish body with no shadows: opened at the low, closed at the high.',
				means: 'Buyers in control all period; strength/continuation.',
				confirm: 'A breakout on a marubozu with volume tends to follow through.'
			}
		}
	},
	{
		id: 'bearish-marubozu',
		bias: 'bearish',
		context: null,
		candles: [[78, 78, 22, 22]],
		text: {
			'pt-BR': {
				name: 'Marubozu de baixa',
				what: 'Corpo grande de baixa sem sombras: abriu na máxima, fechou na mínima.',
				means: 'Vendedores no controle o período inteiro; força/continuação.',
				confirm: 'Perda de suporte com marubozu e volume tende a ter continuidade.'
			},
			en: {
				name: 'Bearish marubozu',
				what: 'Large bearish body with no shadows: opened at the high, closed at the low.',
				means: 'Sellers in control all period; strength/continuation.',
				confirm: 'A support break on a marubozu with volume tends to follow through.'
			}
		}
	}
];

/** Three small candles that set the trend context before a pattern (schematic). */
export function contextCandles(context: 'up' | 'down' | null, first: Candle): Candle[] {
	if (!context) return [];
	const step = context === 'down' ? 9 : -9;
	const out: Candle[] = [];
	for (let k = 3; k >= 1; k--) {
		const mid = (first[0] + first[3]) / 2 + step * k;
		const [o, c] = context === 'down' ? [mid + 4, mid - 4] : [mid - 4, mid + 4];
		out.push([o, Math.max(o, c) + 2, Math.min(o, c) - 2, c]);
	}
	return out;
}
