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
	},
	// Strategies (add-academy-and-strategies design D8): rubrics shipped reviewed: false.
	'm13-call-spread': {
		'spread-breakeven': q(
			{
				prompt: 'Trava de alta: call de strike 100 comprada e call de strike 105 vendida, com débito líquido de 2,10. Qual é o ponto de equilíbrio no vencimento?',
				choices: []
			},
			{
				prompt: 'Bull call spread: a 100-strike call bought and a 105-strike call written, for a net debit of 2.10. What is its breakeven at expiry?',
				choices: []
			}
		),
		'spread-max-profit': q(
			{
				prompt: 'Na mesma trava 100/105 com débito de 2,10, qual é o lucro máximo por unidade no vencimento?',
				choices: ['Ilimitado, como o de uma call comprada', '2,10: o débito pago', '2,90: a largura (5) menos o débito']
			},
			{
				prompt: 'In the same 100/105 spread for a 2.10 debit, what is the maximum profit per unit at expiry?',
				choices: ['Unlimited, like a bought call', '2.10: the debit paid', '2.90: the width (5) minus the debit']
			}
		)
	},
	'm14-covered-call': {
		'covered-max-profit': q(
			{ prompt: 'Long de 1 unidade a 100 e call de strike 105 vendida por 2. Qual é o lucro máximo por unidade no vencimento, sem custos?', choices: [] },
			{ prompt: 'Long 1 unit at 100 and a 105-strike call written for 2. What is the maximum profit per unit at expiry, without costs?', choices: [] }
		),
		'covered-fall': q(
			{
				prompt: 'Numa venda coberta, o que protege a posição se o preço cair 20%?',
				choices: ['Só o prêmio recebido: o long sofre quase toda a queda', 'A call vendida, que ganha o que o long perde', 'A margem da call, que é devolvida']
			},
			{
				prompt: 'In a covered call, what protects the position if the price falls 20%?',
				choices: ['Only the premium collected: the long takes almost the whole fall', 'The written call, which gains what the long loses', 'The call’s margin, which is returned']
			}
		)
	},
	'm15-protective-put': {
		'put-floor': q(
			{ prompt: 'Long de 1 unidade a 100 com put de strike 95 comprada por 2. Perda máxima por unidade até o vencimento, sem custos?', choices: [] },
			{ prompt: 'Long 1 unit at 100 with a 95-strike put bought for 2. Maximum loss per unit to expiry, without costs?', choices: [] }
		),
		'calm-put-loss': q(
			{
				prompt: 'Numa partida calma, a put de proteção terminou com prejuízo. Por quê?',
				choices: ['A put estava com o strike errado', 'O prêmio pago era valor de tempo, que o theta consumiu até o vencimento', 'A corretora cobrou margem da put']
			},
			{
				prompt: 'In a calm run, the protective put ended at a loss. Why?',
				choices: ['The put had the wrong strike', 'The premium paid was time value, which theta ate away by expiry', 'The exchange charged margin on the put']
			}
		)
	},
	'm16-range-condor': {
		'condor-max-loss': q(
			{
				prompt: 'Iron condor 90/95/105/110 (puts 90 comprada e 95 vendida, calls 105 vendida e 110 comprada) com crédito líquido de 3. Perda máxima por unidade no vencimento, sem custos?',
				choices: []
			},
			{
				prompt: 'Iron condor 90/95/105/110 (puts: 90 bought, 95 written; calls: 105 written, 110 bought) for a net credit of 3. Maximum loss per unit at expiry, without costs?',
				choices: []
			}
		),
		'unbounded-structure': q(
			{
				prompt: 'Qual destas estruturas tem perda sem limite definido?',
				choices: ['O iron condor', 'A trava de alta com calls', 'O strangle vendido']
			},
			{ prompt: 'Which of these structures has a loss with no defined limit?', choices: ['The iron condor', 'The bull call spread', 'The short strangle'] }
		)
	},
	'm17-before-expiry': {
		'theta-near-expiry': q(
			{
				prompt: 'O que acontece com o theta por barra de uma opção no dinheiro nas últimas barras antes do vencimento?',
				choices: ['Cresce: o valor de tempo restante cai cada vez mais rápido', 'Diminui até zero de forma constante', 'Não muda: o theta só depende da volatilidade']
			},
			{
				prompt: 'What happens to an at-the-money option’s theta per bar in the last bars before expiry?',
				choices: ['It grows: the time value left falls faster and faster', 'It shrinks steadily to zero', 'It does not change: theta depends only on volatility']
			}
		),
		'what-is-roll': q(
			{
				prompt: 'O que é rolar uma opção?',
				choices: ['Adiar o vencimento da mesma opção sem custo', 'Fechar a posição de um vencimento e abrir outra num vencimento seguinte: duas operações', 'Trocar a call por uma put do mesmo strike']
			},
			{
				prompt: 'What is rolling an option?',
				choices: ['Postponing the same option’s expiry at no cost', 'Closing the position in one expiry and opening another in a later one: two trades', 'Swapping the call for a put at the same strike']
			}
		)
	},
	'm18-gamma-scalp': {
		'hedge-units': q(
			{ prompt: 'Um straddle tem delta de +0,20 por unidade, em 50 unidades. Quantas unidades do perpétuo vender para ficar neutro em delta?', choices: [], unit: 'unidades' },
			{ prompt: 'A straddle has a delta of +0.20 per unit on 50 units. How many perpetual units to sell to be delta-neutral?', choices: [], unit: 'units' }
		),
		'what-pays-theta': q(
			{ prompt: 'O que paga o theta de um straddle comprado e neutralizado em delta?', choices: ['A queda da volatilidade implícita depois da compra', 'O funding do perpétuo usado no hedge', 'Movimentos realizados maiores que a volatilidade implícita cobrada'] },
			{ prompt: 'What pays the theta of a delta-hedged long straddle?', choices: ['Implied volatility falling after the purchase', 'The funding of the perpetual used to hedge', 'Realized moves larger than the implied volatility priced in'] }
		)
	},
	'm19-funding-carry': {
		'funding-one-day': q(
			{ prompt: 'Um short de 20 unidades a 100 recebe +0,03% por intervalo, 3 intervalos por dia. Quanto recebe de funding num dia?', choices: [], unit: 'cr' },
			{ prompt: 'A short of 20 units at 100 receives +0.03% per interval, 3 intervals a day. How much funding does it receive in a day?', choices: [], unit: 'cr' }
		),
		'carry-turns': q(
			{ prompt: 'Quando o carrego de um short no perpétuo se volta contra quem carrega?', choices: ['Quando o funding fica negativo: os shorts passam a pagar', 'Quando o preço sobe, mesmo com o hedge no lugar', 'Nunca: com o hedge, o resultado é fixo'] },
			{ prompt: 'When does a short-perpetual carry turn against the holder?', choices: ['When funding turns negative: shorts start paying', 'When the price rises, even with the hedge in place', 'Never: with the hedge the result is locked in'] }
		)
	},
	'm20-event-vol': {
		'straddle-upper-breakeven': q(
			{ prompt: 'Um straddle no dinheiro de strike 100 custou 6. Qual é o ponto de equilíbrio de cima no vencimento?', choices: [] },
			{ prompt: 'An at-the-money 100 straddle cost 6. What is the upper breakeven at expiry?', choices: [] }
		),
		'iv-after-release': q(
			{ prompt: 'O que acontece com a volatilidade implícita logo depois da divulgação?', choices: ['Sobe, porque o preço se mexeu', 'Cai, qualquer que seja o resultado', 'Fica igual até o vencimento'] },
			{ prompt: 'What happens to implied volatility right after the release?', choices: ['It rises, because the price moved', 'It falls, whatever the outcome', 'It stays the same until expiry'] }
		)
	},
	'm21-calendar': {
		'calendar-debit': q(
			{ prompt: 'Venda a call 100 curta por 2,00 e compre a call 100 longa por 3,50. Qual é o débito líquido, a perda máxima no vencimento curto?', choices: [] },
			{ prompt: 'Sell the near 100 call for 2.00 and buy the far 100 call for 3.50. What is the net debit, the maximum loss at the near expiry?', choices: [] }
		),
		'calendar-best-price': q(
			{ prompt: 'Onde um calendário comprado lucra mais no vencimento curto?', choices: ['Com o preço perto do strike', 'Com o preço muito acima do strike', 'Com o preço muito abaixo do strike'] },
			{ prompt: 'Where does a long calendar profit most at the near expiry?', choices: ['With the price near the strike', 'With the price far above the strike', 'With the price far below the strike'] }
		)
	},
	'm22-scale-manage': {
		'risk-after-partial': q(
			{ prompt: 'Long de 30 a 100 com stop em 96. Você vende 15 a 104 e leva o stop a 100. Qual é o pior resultado que sobra para o trade, antes dos custos?', choices: [], unit: 'cr' },
			{ prompt: 'Long 30 at 100 with a stop at 96. You sell 15 at 104 and move the stop to 100. What is the worst result left for the trade, before costs?', choices: [], unit: 'cr' }
		),
		'breakeven-stop-cost': q(
			{ prompt: 'Do que você abre mão ao levar o stop ao ponto de entrada?', choices: ['De nada: o trade não pode mais perder e não pode ser parado', 'Do lucro já realizado na saída parcial', 'Pode ser parado num recuo normal de uma tendência que estava certa'] },
			{ prompt: 'What does moving the stop to breakeven give up?', choices: ['Nothing: the trade can no longer lose and cannot be stopped', 'The profit already taken on the partial exit', 'It can be stopped out on a normal pullback of a trend that was right'] }
		)
	},
	'm23-second-chance': {
		'drawdown-after-doubling': q(
			{ prompt: 'Duas perdas de 1%, e o terceiro trade, dimensionado em 2%, também perde. Qual é a queda total, sem juros compostos (em %)?', choices: [], unit: '%' },
			{ prompt: 'Two 1% losses, then the third trade, sized at 2%, also loses. What is the total drawdown before compounding (in %)?', choices: [], unit: '%' }
		),
		'after-stop-out': q(
			{ prompt: 'Qual é o passo sensato depois de um stop?', choices: ['Entrar de novo na hora, com mais tamanho, para recuperar', 'Esperar o setup se formar de novo e entrar com o tamanho normal', 'Tirar o stop no próximo trade para não ser parado de novo'] },
			{ prompt: 'What is the sound step after a stop-out?', choices: ['Get back in at once, bigger, to win it back', 'Wait for the setup to form again and enter at normal size', 'Drop the stop on the next trade so it cannot be hit again'] }
		)
	},
	'm24-fast-market': {
		'chase-quantity': q(
			{ prompt: 'Entrada em 112, invalidação em 104 e orçamento de risco de 100. Qual é a maior quantidade?', choices: [], unit: 'unidades' },
			{ prompt: 'Entry 112, invalidation 104 and a risk budget of 100. What is the largest quantity?', choices: [], unit: 'units' }
		),
		'why-chase-worse': q(
			{ prompt: 'Por que uma entrada perseguindo o preço é pior para o mesmo risco?', choices: ['O stop fica mais longe, então o tamanho é menor ou a relação ganho/risco piora', 'O spread é sempre maior numa alta', 'Não é pior: o movimento confirma a entrada'] },
			{ prompt: 'Why is a chased entry worse for the same risk?', choices: ['The stop sits farther away, so the size is smaller or the reward to risk is worse', 'The spread is always wider in a rally', 'It is not worse: the move confirms the entry'] }
		)
	},
	'm25-the-dip': {
		'average-down-loss': q(
			{ prompt: 'Long de 10 a 100, você aumenta 10 a 90, stop em 85. Qual é a perda no stop?', choices: [], unit: 'cr' },
			{ prompt: 'Long 10 at 100, you add 10 at 90, stop at 85. What is the loss at the stop?', choices: [], unit: 'cr' }
		),
		'consistent-add': q(
			{ prompt: 'Quando aumentar uma posição perdedora é coerente com um plano?', choices: ['Sempre que o preço estiver mais barato que a entrada', 'Quando o prejuízo ainda é pequeno', 'Quando o aumento foi planejado na entrada, com o risco total até a mesma invalidação'] },
			{ prompt: 'When is adding to a losing position consistent with a plan?', choices: ['Whenever the price is cheaper than the entry', 'While the loss is still small', 'When the add was planned at entry, with the total risk to the same invalidation'] }
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
