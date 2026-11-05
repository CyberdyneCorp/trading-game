// Lesson question texts (task 4.5). The engine holds each mission's questions, their kind, number of
// options, points and answer key (engine/rubrics/lessons.json, reviewed by a human before they score);
// this catalog holds the words, by mission and question id, with the options in the key's order.
// Numeric answers are typed in the player's locale (decimal comma in PT-BR) and sent as decimals.

import type { LessonInfo, LessonResultView } from '#lib/engine/types.ts';
import { fmtNum, parseDecimal } from './format.ts';
import type { Locale } from './i18n.svelte';

export interface QuestionText {
	prompt: string;
	/** Multiple choice: the options, in the engine's order. Numeric: empty. */
	choices: string[];
	/** Numeric: the unit shown next to the field (e.g. "contratos"). */
	unit?: string;
}

type MissionQuestions = Record<string, Record<Locale, QuestionText>>;

const q = (pt: QuestionText, en: QuestionText): Record<Locale, QuestionText> => ({ 'pt-BR': pt, en });

const TEXTS: Record<string, MissionQuestions> = {
	'm01-false-breakout': {
		'breakout-plan': q(
			{
				prompt: 'O preço rompe o topo de um range sem notícia nova. O que o plano precisa ter antes de comprar o rompimento?',
				choices: ['Nada: rompimento é sinal de compra', 'Um ponto de invalidação (stop) e o risco calculado', 'A alavancagem máxima, para aproveitar o movimento']
			},
			{
				prompt: 'Price breaks the top of a range with no new headline. What must the plan have before buying the breakout?',
				choices: ['Nothing: a breakout is a buy signal', 'An invalidation level (stop) and the risk computed', 'Maximum leverage, to make the most of the move']
			}
		),
		'risk-two-percent': q(
			{ prompt: 'Com patrimônio de 10.000, quanto é um risco de 2% por ideia?', choices: [] },
			{ prompt: 'With equity of 10,000, how much is a risk of 2% per idea?', choices: [] }
		)
	},
	'm02-funding-squeeze': {
		'funding-paid': q(
			{ prompt: 'Um long de 10.000 de nocional paga funding de 0,03% a cada intervalo. Quanto paga em 4 intervalos?', choices: [] },
			{ prompt: 'A long with 10,000 of notional pays 0.03% funding each interval. How much does it pay over 4 intervals?', choices: [] }
		),
		'crowded-funding': q(
			{
				prompt: 'Funding positivo e alto significa que:',
				choices: ['O preço vai cair em seguida', 'Os longs pagam aos shorts para ficar posicionados: a multidão está comprada', 'Ficar long não custa nada']
			},
			{
				prompt: 'High positive funding means that:',
				choices: ['Price will fall next', 'Longs pay shorts to stay positioned: the crowd is long', 'Staying long costs nothing']
			}
		)
	},
	'm03-volatility-shock': {
		'liquidation-price': q(
			{ prompt: 'Long a 100 com 10× de alavancagem e margem de manutenção de 0,5%: o preço de liquidação fica perto de quanto?', choices: [] },
			{ prompt: 'A long at 100 with 10× leverage and 0.5% maintenance margin: the liquidation price is close to what?', choices: [] }
		),
		'leverage-for-stop': q(
			{
				prompt: 'Seu stop está 8% abaixo da entrada (margem de manutenção 0,5%). Qual é a maior alavancagem entre estas que mantém a liquidação abaixo do stop?',
				choices: ['20×', '10×', '5×']
			},
			{
				prompt: 'Your stop is 8% below the entry (0.5% maintenance margin). Which is the highest of these leverages that keeps liquidation below the stop?',
				choices: ['20×', '10×', '5×']
			}
		)
	},
	'm04-options-buyer': {
		'call-breakeven': q(
			{ prompt: 'Call de strike 105 comprada por 2,50. Qual é o ponto de equilíbrio no vencimento?', choices: [] },
			{ prompt: 'A 105-strike call bought for 2.50. What is the breakeven at expiry?', choices: [] }
		),
		'buyer-max-loss': q(
			{ prompt: 'Qual é a perda máxima de quem compra uma opção?', choices: ['Ilimitada', 'O prêmio pago (mais taxas)', 'A margem de manutenção'] },
			{ prompt: 'What is the maximum loss of an option buyer?', choices: ['Unlimited', 'The premium paid (plus fees)', 'The maintenance margin'] }
		)
	},
	'm05-options-writer': {
		'put-writer-risk': q(
			{
				prompt: 'Quem vende uma put descoberta perde mais quando:',
				choices: ['O preço sobe muito', 'O preço cai muito abaixo do strike', 'A volatilidade implícita cai']
			},
			{ prompt: 'A naked put writer loses most when:', choices: ['Price rises a lot', 'Price falls far below the strike', 'Implied volatility falls'] }
		),
		'theta-side': q(
			{
				prompt: 'Com o mercado parado e o tempo passando:',
				choices: ['O vendedor da opção ganha (theta)', 'O comprador da opção ganha', 'O prêmio não muda']
			},
			{ prompt: 'With the market still and time passing:', choices: ['The option writer gains (theta)', 'The option buyer gains', 'The premium does not change'] }
		)
	},
	'm06-thin-book': {
		'market-order-cost': q(
			{ prompt: 'Meio spread de 0,10% e slippage de 0,20%: quanto custa, em %, só a entrada de uma ordem a mercado?', choices: [], unit: '%' },
			{ prompt: 'Half-spread 0.10% and slippage 0.20%: how much does the entry alone of a market order cost, in %?', choices: [], unit: '%' }
		),
		'unfilled-limit': q(
			{
				prompt: 'Sua limitada na zona não executou e o preço foi embora. Isso foi:',
				choices: ['Um prejuízo igual ao movimento perdido', 'Um trade perdido, não um prejuízo', 'Um erro: deveria ter usado ordem a mercado']
			},
			{
				prompt: 'Your limit at the zone did not fill and price moved away. That was:',
				choices: ['A loss equal to the move you missed', 'A missed trade, not a loss', 'A mistake: you should have used a market order']
			}
		)
	},
	'm07-trend-trailing': {
		'size-from-stop': q(
			{ prompt: 'Patrimônio 10.000, risco de 1%, stop 2,00 abaixo da entrada. Quantos contratos (de 1 unidade)?', choices: [], unit: 'contratos' },
			{ prompt: 'Equity 10,000, 1% risk, stop 2.00 below the entry. How many contracts (of 1 unit)?', choices: [], unit: 'contracts' }
		),
		'tight-trail': q(
			{
				prompt: 'Numa tendência com recuos de 1 a 3 ATR, um stop móvel de 1×ATR costuma:',
				choices: ['Ficar no movimento até o fim', 'Ser tirado por um recuo comum', 'Nunca executar']
			},
			{
				prompt: 'In a trend with pullbacks of 1 to 3 ATR, a 1×ATR trailing stop usually:',
				choices: ['Stays in the move until the end', 'Gets taken out by an ordinary pullback', 'Never fills']
			}
		)
	},
	'm08-regime-switch': {
		'rsi-in-trend': q(
			{
				prompt: 'O RSI está acima de 70 numa tendência forte de alta. Vender por sobrecompra:',
				choices: ['Costuma funcionar em tendência', 'Costuma perder em tendência e funciona melhor na lateral', 'É sempre seguro com stop']
			},
			{
				prompt: 'RSI is above 70 in a strong uptrend. Selling the overbought reading:',
				choices: ['Usually works in a trend', 'Usually loses in a trend and works better in a range', 'Is always safe with a stop']
			}
		),
		'filter-lag': q(
			{ prompt: 'Quando o regime muda, ADX e largura das bandas:', choices: ['Avisam antes da mudança', 'Confirmam com atraso', 'Não mudam'] },
			{ prompt: 'When the regime changes, ADX and band width:', choices: ['Warn before the change', 'Confirm it late', 'Do not change'] }
		)
	},
	'm09-event-straddle': {
		'straddle-upper-breakeven': q(
			{ prompt: 'Straddle de strike 100 comprado por 6,00 no total. Qual é o ponto de equilíbrio de cima no vencimento?', choices: [] },
			{ prompt: 'A 100-strike straddle bought for 6.00 in total. What is the upper breakeven at expiry?', choices: [] }
		),
		'late-straddle': q(
			{
				prompt: 'Comprar o straddle logo depois da divulgação:',
				choices: ['Paga a queda da IV e perde o gap', 'É mais barato e ainda pega o gap', 'Não tem theta']
			},
			{
				prompt: 'Buying the straddle right after the release:',
				choices: ['Pays for the IV crush and misses the gap', 'Is cheaper and still catches the gap', 'Has no theta']
			}
		)
	},
	'm10-hedged-book': {
		'protective-put-loss': q(
			{ prompt: 'Long de 1 unidade a 100 com put de strike 95 comprada por 2. Perda máxima por unidade até o vencimento, sem custos?', choices: [] },
			{ prompt: 'Long 1 unit at 100 with a 95-strike put bought for 2. Maximum loss per unit to expiry, without costs?', choices: [] }
		),
		'collar-trade': q(
			{
				prompt: 'Um collar (put comprada e call vendida) troca:',
				choices: ['Proteção na queda por ganho limitado na alta', 'Ganho ilimitado por risco ilimitado', 'Funding por theta']
			},
			{ prompt: 'A collar (a bought put and a written call) trades:', choices: ['Downside protection for a capped upside', 'Unlimited gain for unlimited risk', 'Funding for theta'] }
		)
	},
	'm11-news-calendar': {
		'in-line-release': q(
			{ prompt: 'Uma divulgação sai igual ao consenso. O que costuma acontecer?', choices: ['Um gap grande', 'Pouco movimento: já estava no preço', 'O preço sempre sobe'] },
			{ prompt: 'A release comes out in line with the consensus. What usually happens?', choices: ['A large gap', 'Little movement: it was already priced', 'Price always rises'] }
		),
		'stop-through-release': q(
			{
				prompt: 'Um stop de 1×ATR mantido durante uma divulgação com surpresa:',
				choices: ['Executa exatamente no preço do stop', 'Pode executar bem além do preço, no gap', 'É cancelado pela bolsa']
			},
			{
				prompt: 'A 1×ATR stop held through a release with a surprise:',
				choices: ['Fills exactly at its price', 'Can fill far beyond its price, in the gap', 'Is cancelled by the exchange']
			}
		)
	},
	'm12-capstone': {
		'one-sample': q(
			{
				prompt: 'Uma missão lucrativa prova que a estratégia tem vantagem?',
				choices: ['Sim', 'Não: uma missão é uma amostra; é preciso comparar muitas tentativas', 'Só se o lucro for alto']
			},
			{
				prompt: 'Does one profitable mission prove the strategy has an edge?',
				choices: ['Yes', 'No: one mission is one sample; many attempts must be compared', 'Only if the profit is large']
			}
		),
		expectancy: q(
			{ prompt: '10 trades: 4 ganharam 2R cada e 6 perderam 1R cada. Qual é a expectativa, em R por trade?', choices: [] },
			{ prompt: '10 trades: 4 won 2R each and 6 lost 1R each. What is the expectancy, in R per trade?', choices: [] }
		)
	}
};

/** The text of a lesson question, or null when the catalog has none (a newer engine's question). */
export function questionText(mission: string, id: string, locale: Locale): QuestionText | null {
	return TEXTS[mission]?.[id]?.[locale] ?? null;
}

/** Missions with lesson texts (for the catalog consistency test). */
export function lessonMissions(): string[] {
	return Object.keys(TEXTS);
}

/** The questions of `info` the client can show (with texts whose option count matches the engine's). */
export function showableQuestions(info: LessonInfo, locale: Locale): { index: number; id: string; text: QuestionText }[] {
	const out: { index: number; id: string; text: QuestionText }[] = [];
	info.questions.forEach((question, index) => {
		const text = questionText(info.mission, question.id, locale);
		const matches = text && (question.kind === 'numeric' ? text.choices.length === 0 : text.choices.length === question.choices);
		if (text && matches) out.push({ index, id: question.id, text });
	});
	return out;
}

/** A numeric answer as typed (decimal comma or point) -> the engine's decimal string, or null when invalid. */
export function numericAnswer(input: string): string | null {
	const v = parseDecimal(input);
	if (!Number.isFinite(v) || v < 0) return null;
	return v.toFixed(8);
}

/** The debrief's given or expected answer as text: the option, or the number in the locale. */
export function answerText(mission: string, r: LessonResultView, which: 'given' | 'answer', locale: Locale): string {
	const v = r[which];
	if (v == null) return '—';
	if (r.kind === 'numeric') return fmtNum(Number(v), locale, Number.isInteger(Number(v)) ? 0 : 2);
	return questionText(mission, r.id, locale)?.choices[Number(v)] ?? String(v);
}
