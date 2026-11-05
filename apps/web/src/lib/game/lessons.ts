// Academy lessons (spec curriculum "Ordered learning stages": concept, demonstration, decision,
// feedback and a transfer exercise on a different market; web-client task 4.4). Lessons teach the
// curriculum stages that have no mission (today Foundations). They run entirely on the client from
// the data below: the charts are small illustrative windows, labeled as such, and the transfer
// exercise always uses a market and window the demonstration did not show. Completion is recorded
// in local progress (`progress.lessonsDone`) and passes the lesson's stage on the curriculum map.

import type { StageId } from './curriculum.ts';
import type { Locale } from './i18n.svelte.ts';

export interface Candle {
	o: number;
	h: number;
	l: number;
	c: number;
	v: number;
}

export interface LessonChart {
	/** Illustrative market id; the transfer exercise uses one the demonstration did not. */
	market: string;
	interval: string;
	candles: Candle[];
	/** 1-based bar to highlight. */
	highlight?: number;
	/** Bars already closed; the rest is the unknown future (hatched, nothing drawn). */
	known?: number;
	/** Draw one large annotated candle (open, high, low, close, body, wicks). */
	anatomy?: boolean;
}

export type Choice = { kind: 'bar'; bar: number } | { kind: 'pct'; value: number } | { kind: 'text'; id: string };

export type LessonPhase = 'concept' | 'demo' | 'decision' | 'transfer';

export interface LessonStep {
	id: string;
	phase: LessonPhase;
	chart?: LessonChart;
	question?: { choices: Choice[]; answer: number };
}

export interface Lesson {
	id: string;
	stage: StageId;
	steps: LessonStep[];
}

export interface StepText {
	title: string;
	body: string;
	/** Feedback after a right / a wrong answer (questions only). */
	right?: string;
	wrong?: string;
	/** Texts of `text` choices, by choice id. */
	choices?: Record<string, string>;
}

export interface LessonText {
	title: string;
	summary: string;
	steps: Record<string, StepText>;
}

const ANATOMY: Candle[] = [{ o: 100, h: 104, l: 98, c: 103, v: 0 }];

/** Market X, 1h: the demonstration window. */
const MARKET_X: Candle[] = [
	{ o: 100.0, h: 101.2, l: 99.4, c: 100.8, v: 120 },
	{ o: 100.8, h: 102.6, l: 100.5, c: 102.2, v: 180 },
	{ o: 102.2, h: 102.9, l: 100.9, c: 101.3, v: 150 },
	{ o: 101.3, h: 101.6, l: 98.6, c: 99.4, v: 260 },
	{ o: 99.4, h: 100.9, l: 99.1, c: 100.6, v: 140 },
	{ o: 100.6, h: 100.8, l: 97.9, c: 100.2, v: 210 },
	{ o: 100.2, h: 101.8, l: 100.0, c: 101.5, v: 170 },
	{ o: 101.5, h: 102.4, l: 101.1, c: 102.0, v: 130 }
];

/** Market Y, 4h, another price level: the transfer window (never shown in the demonstration). */
const MARKET_Y: Candle[] = [
	{ o: 2410, h: 2432, l: 2398, c: 2425, v: 35 },
	{ o: 2425, h: 2441, l: 2380, c: 2391, v: 52 },
	{ o: 2391, h: 2402, l: 2350, c: 2362, v: 61 },
	{ o: 2362, h: 2399, l: 2355, c: 2394, v: 44 },
	{ o: 2394, h: 2418, l: 2388, c: 2412, v: 38 },
	{ o: 2412, h: 2415, l: 2371, c: 2378, v: 49 }
];

const x = (extra: Partial<LessonChart> = {}): LessonChart => ({ market: 'X', interval: '1h', candles: MARKET_X, ...extra });
const y = (extra: Partial<LessonChart> = {}): LessonChart => ({ market: 'Y', interval: '4h', candles: MARKET_Y, ...extra });

export const LESSONS: readonly Lesson[] = [
	{
		id: 'foundations-bar',
		stage: 'foundations',
		steps: [
			{ id: 'anatomy', phase: 'concept', chart: { market: 'X', interval: '1h', candles: ANATOMY, anatomy: true } },
			{ id: 'interval', phase: 'concept', chart: x() },
			{ id: 'return', phase: 'demo', chart: x({ highlight: 2 }) },
			{
				id: 'wick',
				phase: 'decision',
				chart: x(),
				question: { choices: [3, 4, 6, 7].map((bar) => ({ kind: 'bar', bar })), answer: 2 }
			},
			{
				id: 'pct',
				phase: 'decision',
				chart: x({ highlight: 5 }),
				question: { choices: [1.21, 0.12, -1.21, 12.07].map((value) => ({ kind: 'pct', value })), answer: 0 }
			},
			{
				id: 'known',
				phase: 'decision',
				chart: x({ known: 6 }),
				question: { choices: ['close6', 'high7', 'dir7', 'wickUp'].map((id) => ({ kind: 'text', id })), answer: 0 }
			},
			{
				id: 'range',
				phase: 'transfer',
				chart: y(),
				question: { choices: [2, 3, 4, 6].map((bar) => ({ kind: 'bar', bar })), answer: 0 }
			},
			{
				id: 'move',
				phase: 'transfer',
				chart: y({ highlight: 4 }),
				question: { choices: [-1.28, 1.28, -0.31, -12.78].map((value) => ({ kind: 'pct', value })), answer: 0 }
			}
		]
	}
];

const TEXTS: Record<string, Record<Locale, LessonText>> = {
	'foundations-bar': {
		'pt-BR': {
			title: 'Ler uma barra',
			summary: 'Abertura, máxima, mínima e fechamento; corpo, pavios e volume; preço contra retorno; o que já se sabe e o que é futuro.',
			steps: {
				anatomy: {
					title: 'Uma barra, quatro preços',
					body: 'Cada barra resume um intervalo: o preço de abertura, a máxima, a mínima e o fechamento. O corpo vai da abertura ao fechamento; os pavios mostram até onde o preço foi. Fechou acima da abertura: barra de alta (verde). Abaixo: de baixa (vermelha).'
				},
				interval: {
					title: 'Intervalo e volume',
					body: 'Neste gráfico cada barra é 1 hora: o intervalo (timeframe) diz quanto tempo cabe em uma barra. As colunas embaixo são o volume, quanto foi negociado naquela hora. Volume alto mostra participação, não direção.'
				},
				return: {
					title: 'Preço contra retorno',
					body: 'A barra 2 subiu de 100,80 para 102,20: 1,40 de preço, ou +1,39% de retorno. Os mesmos 1,40 em um mercado a 2.400 seriam só 0,06%. Compare movimentos em retorno (%), não em pontos.'
				},
				wick: {
					title: 'Decisão: leia os pavios',
					body: 'Qual barra fechou abaixo da abertura e tem o pavio inferior mais longo?',
					right: 'Isso. A barra 6 abriu em 100,60, foi até 97,90 e fechou em 100,20: pavio inferior de 2,30. Vendedores empurraram, compradores trouxeram de volta.',
					wrong: 'Ainda não. O pavio inferior vai do menor entre abertura e fechamento até a mínima. Compare as barras de baixa (fechamento abaixo da abertura).'
				},
				pct: {
					title: 'Decisão: calcule o retorno',
					body: 'A barra 5 abriu em 99,40 e fechou em 100,60. Qual foi o retorno dela?',
					right: 'Certo: (100,60 − 99,40) / 99,40 = +1,21%.',
					wrong: 'Retorno = (fechamento − abertura) / abertura. Aqui: 1,20 / 99,40.'
				},
				known: {
					title: 'Decisão: o que você já sabe?',
					body: 'A barra 6 acabou de fechar. A área hachurada é o futuro. O que você sabe agora?',
					right: 'Exato. Só o que já fechou é informação. A máxima da barra 7 e a direção dela ainda não existem, e um pavio longo não garante nada: padrões falham.',
					wrong: 'Nada depois da barra 6 é conhecido. Um pavio longo é uma pista, não uma garantia: padrões falham com frequência.',
					choices: {
						close6: 'O fechamento da barra 6',
						high7: 'A máxima da barra 7',
						dir7: 'Se a barra 7 vai fechar em alta',
						wickUp: 'Que o pavio longo da barra 6 garante uma alta'
					}
				},
				range: {
					title: 'Transferência: outro mercado',
					body: 'Mercado Y, barras de 4 horas, preço perto de 2.400. Qual barra teve a maior amplitude (máxima − mínima)?',
					right: 'Sim: a barra 2 foi de 2.441 a 2.380, amplitude de 61. As mesmas regras de leitura valem em qualquer mercado e intervalo.',
					wrong: 'Amplitude = máxima − mínima, incluindo os pavios. Meça cada barra.'
				},
				move: {
					title: 'Transferência: retorno em outro nível de preço',
					body: 'Do fechamento da barra 1 (2.425) ao fechamento da barra 4 (2.394), qual foi o retorno?',
					right: 'Certo: −31 / 2.425 = −1,28%. No mercado X, 31 pontos seriam cerca de 30%: por isso se compara em retorno.',
					wrong: 'Use os fechamentos: (2.394 − 2.425) / 2.425.'
				}
			}
		},
		en: {
			title: 'Reading a bar',
			summary: 'Open, high, low and close; body, wicks and volume; price against return; what is known and what is future.',
			steps: {
				anatomy: {
					title: 'One bar, four prices',
					body: 'Each bar sums up one interval: the open, the high, the low and the close. The body runs from open to close; the wicks show how far price went. Closed above the open: an up bar (green). Below: a down bar (red).'
				},
				interval: {
					title: 'Interval and volume',
					body: 'On this chart each bar is 1 hour: the interval (timeframe) says how much time one bar holds. The columns below are volume, how much traded in that hour. High volume shows participation, not direction.'
				},
				return: {
					title: 'Price against return',
					body: 'Bar 2 rose from 100.80 to 102.20: 1.40 in price, or a +1.39% return. The same 1.40 in a market at 2,400 would be only 0.06%. Compare moves as returns (%), not points.'
				},
				wick: {
					title: 'Decision: read the wicks',
					body: 'Which bar closed below its open and has the longest lower wick?',
					right: 'Right. Bar 6 opened at 100.60, went down to 97.90 and closed at 100.20: a 2.30 lower wick. Sellers pushed, buyers brought it back.',
					wrong: 'Not yet. The lower wick runs from the lower of open and close down to the low. Compare the down bars (close below open).'
				},
				pct: {
					title: 'Decision: work out the return',
					body: 'Bar 5 opened at 99.40 and closed at 100.60. What was its return?',
					right: 'Right: (100.60 − 99.40) / 99.40 = +1.21%.',
					wrong: 'Return = (close − open) / open. Here: 1.20 / 99.40.'
				},
				known: {
					title: 'Decision: what do you know?',
					body: 'Bar 6 has just closed. The hatched area is the future. What do you know now?',
					right: 'Exactly. Only what has closed is information. Bar 7’s high and direction do not exist yet, and a long wick guarantees nothing: patterns fail.',
					wrong: 'Nothing after bar 6 is known. A long wick is a clue, not a guarantee: patterns fail often.',
					choices: {
						close6: 'Bar 6’s close',
						high7: 'Bar 7’s high',
						dir7: 'Whether bar 7 will close up',
						wickUp: 'That bar 6’s long wick guarantees a rise'
					}
				},
				range: {
					title: 'Transfer: another market',
					body: 'Market Y, 4-hour bars, price near 2,400. Which bar had the widest range (high − low)?',
					right: 'Yes: bar 2 went from 2,441 to 2,380, a range of 61. The same reading rules hold in any market and interval.',
					wrong: 'Range = high − low, wicks included. Measure each bar.'
				},
				move: {
					title: 'Transfer: return at another price level',
					body: 'From the close of bar 1 (2,425) to the close of bar 4 (2,394), what was the return?',
					right: 'Right: −31 / 2,425 = −1.28%. In market X, 31 points would be about 30%: that is why moves are compared as returns.',
					wrong: 'Use the closes: (2,394 − 2,425) / 2,425.'
				}
			}
		}
	}
};

export function lessonById(id: string): Lesson | null {
	return LESSONS.find((l) => l.id === id) ?? null;
}

export function lessonText(id: string, locale: Locale): LessonText {
	const t = TEXTS[id];
	if (!t) throw new Error(`no texts for lesson ${id}`);
	return t[locale];
}

/** Lessons of a curriculum stage, in order. */
export function lessonsOf(stage: StageId): Lesson[] {
	return LESSONS.filter((l) => l.stage === stage);
}

export const isQuestion = (s: LessonStep) => s.question !== undefined;

/** Questions answered right on the first try, out of all questions. */
export function firstTryScore(lesson: Lesson, firstTry: ReadonlyMap<string, boolean>): { right: number; total: number } {
	const questions = lesson.steps.filter(isQuestion);
	return { right: questions.filter((s) => firstTry.get(s.id) === true).length, total: questions.length };
}
