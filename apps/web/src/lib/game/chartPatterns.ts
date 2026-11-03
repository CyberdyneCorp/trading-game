// Chart patterns (structures over many bars) for the cheat sheet (spec terminal-ui
// "Candle pattern cheat sheet"). Drawings are schematic: a price path plus dashed reference lines
// (necklines, trendlines) and the usual breakout direction, on a 0..100 scale (y up).

import type { Locale } from './i18n.svelte.ts';
import type { PatternBias } from './candlePatterns.ts';

export type Point = [x: number, y: number];
export type Segment = [x1: number, y1: number, x2: number, y2: number];

interface Text {
	name: string;
	what: string;
	means: string;
	confirm: string;
}

export interface ChartPattern {
	id: string;
	bias: PatternBias;
	path: Point[];
	lines: Segment[];
	breakout: Segment; // arrow from the breakout point toward the usual move
	text: Record<Locale, Text>;
}

export const CHART_PATTERNS: ChartPattern[] = [
	{
		id: 'double-top',
		bias: 'bearish',
		path: [[0, 30], [20, 75], [38, 50], [56, 75], [72, 50], [78, 42]],
		lines: [[20, 50, 80, 50]],
		breakout: [78, 46, 96, 22],
		text: {
			'pt-BR': {
				name: 'Topo duplo',
				what: 'Dois topos no mesmo nível separados por um vale; a linha do vale é o "pescoço".',
				means: 'O preço falhou duas vezes na mesma resistência: a demanda está se esgotando.',
				confirm: 'Só vale quando fecha abaixo do vale. Alvo comum: altura do padrão projetada para baixo; stop acima do segundo topo.'
			},
			en: {
				name: 'Double top',
				what: 'Two peaks at the same level separated by a trough; the trough line is the "neckline".',
				means: 'Price failed twice at the same resistance: demand is running out.',
				confirm: 'Only valid on a close below the trough. Common target: the pattern height projected down; stop above the second peak.'
			}
		}
	},
	{
		id: 'double-bottom',
		bias: 'bullish',
		path: [[0, 70], [20, 25], [38, 50], [56, 25], [72, 50], [78, 58]],
		lines: [[20, 50, 80, 50]],
		breakout: [78, 54, 96, 78],
		text: {
			'pt-BR': {
				name: 'Fundo duplo',
				what: 'Dois fundos no mesmo nível separados por um pico; a linha do pico é o pescoço.',
				means: 'O preço segurou duas vezes no mesmo suporte: a oferta está se esgotando.',
				confirm: 'Só vale quando fecha acima do pico. Alvo comum: altura do padrão projetada para cima; stop abaixo do segundo fundo.'
			},
			en: {
				name: 'Double bottom',
				what: 'Two troughs at the same level separated by a peak; the peak line is the neckline.',
				means: 'Price held twice at the same support: supply is running out.',
				confirm: 'Only valid on a close above the peak. Common target: the pattern height projected up; stop below the second trough.'
			}
		}
	},
	{
		id: 'head-and-shoulders',
		bias: 'bearish',
		path: [[0, 25], [14, 62], [26, 45], [42, 82], [58, 45], [72, 62], [84, 45], [88, 38]],
		lines: [[20, 45, 92, 45]],
		breakout: [88, 42, 99, 20],
		text: {
			'pt-BR': {
				name: 'Ombro-cabeça-ombro',
				what: 'Três topos: o do meio (cabeça) mais alto que os laterais (ombros), com uma linha de pescoço ligando os vales.',
				means: 'A alta fez um último topo mais alto e depois não conseguiu repetir: tendência perdendo força.',
				confirm: 'Confirma no fechamento abaixo do pescoço; é comum um reteste do pescoço. Stop acima do ombro direito.'
			},
			en: {
				name: 'Head and shoulders',
				what: 'Three peaks: the middle one (head) higher than the side ones (shoulders), with a neckline joining the troughs.',
				means: 'The rally made one last higher high and then could not repeat it: the trend is fading.',
				confirm: 'Confirmed on a close below the neckline; a retest of the neckline is common. Stop above the right shoulder.'
			}
		}
	},
	{
		id: 'inverse-head-and-shoulders',
		bias: 'bullish',
		path: [[0, 75], [14, 38], [26, 55], [42, 18], [58, 55], [72, 38], [84, 55], [88, 62]],
		lines: [[20, 55, 92, 55]],
		breakout: [88, 58, 99, 80],
		text: {
			'pt-BR': {
				name: 'Ombro-cabeça-ombro invertido',
				what: 'Três fundos: o do meio mais baixo que os laterais, com pescoço ligando os picos.',
				means: 'A queda fez um último fundo mais baixo e depois não conseguiu repetir: possível reversão para alta.',
				confirm: 'Confirma no fechamento acima do pescoço, idealmente com volume. Stop abaixo do ombro direito.'
			},
			en: {
				name: 'Inverse head and shoulders',
				what: 'Three troughs: the middle one lower than the side ones, with a neckline joining the peaks.',
				means: 'The decline made one last lower low and then could not repeat it: a possible bullish reversal.',
				confirm: 'Confirmed on a close above the neckline, ideally on volume. Stop below the right shoulder.'
			}
		}
	},
	{
		id: 'bull-flag',
		bias: 'bullish',
		path: [[0, 15], [30, 70], [40, 62], [48, 66], [56, 58], [64, 62], [70, 56], [76, 66]],
		lines: [[30, 70, 76, 60], [38, 62, 76, 52]],
		breakout: [76, 64, 96, 92],
		text: {
			'pt-BR': {
				name: 'Bandeira de alta',
				what: 'Subida forte (o mastro) seguida de um canal curto e levemente descendente (a bandeira).',
				means: 'Pausa para respirar dentro de uma tendência forte; os compradores seguem no controle.',
				confirm: 'Entrada no rompimento da bandeira para cima; stop abaixo da bandeira. Alvo comum: altura do mastro.'
			},
			en: {
				name: 'Bull flag',
				what: 'A sharp rise (the pole) followed by a short, slightly falling channel (the flag).',
				means: 'A breather inside a strong trend; buyers remain in control.',
				confirm: 'Enter on the upside break of the flag; stop below the flag. Common target: the pole height.'
			}
		}
	},
	{
		id: 'bear-flag',
		bias: 'bearish',
		path: [[0, 85], [30, 30], [40, 38], [48, 34], [56, 42], [64, 38], [70, 44], [76, 34]],
		lines: [[30, 30, 76, 40], [38, 38, 76, 48]],
		breakout: [76, 36, 96, 8],
		text: {
			'pt-BR': {
				name: 'Bandeira de baixa',
				what: 'Queda forte (mastro) seguida de um canal curto e levemente ascendente.',
				means: 'Repique fraco dentro de uma tendência de baixa forte.',
				confirm: 'Entrada na perda da bandeira; stop acima dela. Alvo comum: altura do mastro.'
			},
			en: {
				name: 'Bear flag',
				what: 'A sharp drop (the pole) followed by a short, slightly rising channel.',
				means: 'A weak bounce inside a strong downtrend.',
				confirm: 'Enter when the flag breaks down; stop above it. Common target: the pole height.'
			}
		}
	},
	{
		id: 'ascending-triangle',
		bias: 'bullish',
		path: [[0, 25], [16, 70], [28, 38], [44, 70], [56, 50], [70, 70], [78, 60], [84, 78]],
		lines: [[10, 70, 88, 70], [20, 30, 84, 62]],
		breakout: [84, 74, 98, 94],
		text: {
			'pt-BR': {
				name: 'Triângulo ascendente',
				what: 'Topos no mesmo nível (resistência plana) e fundos cada vez mais altos.',
				means: 'Os compradores aceitam pagar mais a cada recuo e pressionam a resistência.',
				confirm: 'Rompimento da resistência com fechamento acima e volume; stop abaixo do último fundo.'
			},
			en: {
				name: 'Ascending triangle',
				what: 'Highs at the same level (flat resistance) and rising lows.',
				means: 'Buyers pay more on every dip and keep pressing the resistance.',
				confirm: 'A close above the resistance on volume; stop below the last higher low.'
			}
		}
	},
	{
		id: 'descending-triangle',
		bias: 'bearish',
		path: [[0, 75], [16, 30], [28, 62], [44, 30], [56, 50], [70, 30], [78, 40], [84, 22]],
		lines: [[10, 30, 88, 30], [20, 70, 84, 38]],
		breakout: [84, 26, 98, 6],
		text: {
			'pt-BR': {
				name: 'Triângulo descendente',
				what: 'Fundos no mesmo nível (suporte plano) e topos cada vez mais baixos.',
				means: 'Os vendedores aceitam vender mais barato a cada repique e pressionam o suporte.',
				confirm: 'Perda do suporte com fechamento abaixo; stop acima do último topo.'
			},
			en: {
				name: 'Descending triangle',
				what: 'Lows at the same level (flat support) and falling highs.',
				means: 'Sellers accept lower prices on every bounce and keep pressing the support.',
				confirm: 'A close below the support; stop above the last lower high.'
			}
		}
	},
	{
		id: 'symmetrical-triangle',
		bias: 'neutral',
		path: [[0, 50], [12, 85], [26, 20], [40, 74], [54, 32], [66, 62], [76, 42], [82, 54]],
		lines: [[8, 88, 90, 52], [20, 16, 90, 48]],
		breakout: [86, 52, 98, 52],
		text: {
			'pt-BR': {
				name: 'Triângulo simétrico',
				what: 'Topos mais baixos e fundos mais altos convergindo: a volatilidade vai se comprimindo.',
				means: 'Indecisão crescente; costuma romper no sentido da tendência anterior, mas não sempre.',
				confirm: 'Espere o rompimento fechar fora do triângulo; tamanho pequeno até lá (rompimentos falsos são comuns).'
			},
			en: {
				name: 'Symmetrical triangle',
				what: 'Lower highs and higher lows converging: volatility keeps compressing.',
				means: 'Growing indecision; it often breaks in the direction of the prior trend, but not always.',
				confirm: 'Wait for a close outside the triangle; keep size small until then (false breakouts are common).'
			}
		}
	}
];
