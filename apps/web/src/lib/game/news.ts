// Fictional headlines for scenario news (spec market-generator "Events": never attributed to real
// people or companies). Text sentiment is only what the player reads, not what the market does.

import type { GameState, NewsView } from '#lib/engine/types.ts';
import type { Locale } from './i18n.svelte';

type Kind = NewsView['kind'];
type Tone = 'pos' | 'neu' | 'neg';

const headlines: Record<Locale, Record<Kind, Record<Tone, string>>> = {
	'pt-BR': {
		rate_decision: {
			pos: 'Banco Central de Varena corta juros',
			neu: 'Banco Central de Varena mantém juros',
			neg: 'Banco Central de Varena sobe juros'
		},
		regulation_rumor: {
			pos: 'Rumor: regulador de Varena prepara regras mais claras para cripto',
			neu: 'Rumor: regulador de Varena estuda novas regras',
			neg: 'Rumor: regulador de Varena estuda restringir derivativos'
		},
		exchange_incident: {
			pos: 'Exchange fictícia KryptoNova normaliza saques',
			neu: 'Exchange fictícia KryptoNova investiga instabilidade',
			neg: 'Exchange fictícia KryptoNova suspende saques após incidente'
		},
		network_outage: {
			pos: 'Rede de liquidação volta ao normal',
			neu: 'Rede de liquidação opera com lentidão',
			neg: 'Rede de liquidação sofre interrupção'
		},
		liquidity_inflow: {
			pos: 'Fundos fictícios anunciam aportes no mercado',
			neu: 'Fluxo de fundos fica estável',
			neg: 'Fundos fictícios registram resgates'
		},
		institutional_adoption: {
			pos: 'Rumor: gestora fictícia Arbor teria comprado grande posição',
			neu: 'Gestora fictícia Arbor avalia o mercado',
			neg: 'Rumor: gestora fictícia Arbor teria vendido posição'
		},
		rumor_denied: {
			pos: 'Gestora fictícia Arbor confirma interesse',
			neu: 'Gestora fictícia Arbor não comenta rumores',
			neg: 'Gestora fictícia Arbor nega rumor de compra'
		},
		liquidity_outflow: {
			pos: 'Formadora de mercado fictícia Lumen amplia suas ofertas',
			neu: 'Formadora de mercado fictícia Lumen revisa suas ofertas',
			neg: 'Formadora de mercado fictícia Lumen reduz suas ofertas no livro'
		}
	},
	en: {
		rate_decision: {
			pos: 'Central Bank of Varena cuts rates',
			neu: 'Central Bank of Varena holds rates',
			neg: 'Central Bank of Varena hikes rates'
		},
		regulation_rumor: {
			pos: 'Rumor: Varena regulator prepares clearer crypto rules',
			neu: 'Rumor: Varena regulator studies new rules',
			neg: 'Rumor: Varena regulator considers restricting derivatives'
		},
		exchange_incident: {
			pos: 'Fictional exchange KryptoNova resumes withdrawals',
			neu: 'Fictional exchange KryptoNova investigates instability',
			neg: 'Fictional exchange KryptoNova halts withdrawals after incident'
		},
		network_outage: {
			pos: 'Settlement network back to normal',
			neu: 'Settlement network running slowly',
			neg: 'Settlement network suffers an outage'
		},
		liquidity_inflow: {
			pos: 'Fictional funds announce inflows',
			neu: 'Fund flows steady',
			neg: 'Fictional funds report outflows'
		},
		institutional_adoption: {
			pos: 'Rumor: fictional manager Arbor reportedly bought a large position',
			neu: 'Fictional manager Arbor reviews the market',
			neg: 'Rumor: fictional manager Arbor reportedly sold its position'
		},
		rumor_denied: {
			pos: 'Fictional manager Arbor confirms interest',
			neu: 'Fictional manager Arbor declines to comment',
			neg: 'Fictional manager Arbor denies buying rumor'
		},
		liquidity_outflow: {
			pos: 'Fictional market maker Lumen widens its quoting',
			neu: 'Fictional market maker Lumen reviews its quoting',
			neg: 'Fictional market maker Lumen pulls quotes from the book'
		}
	}
};

const scale: Record<Locale, string[]> = {
	'pt-BR': ['muito negativo', 'negativo', 'neutro', 'positivo', 'muito positivo'],
	en: ['very negative', 'negative', 'neutral', 'positive', 'very positive']
};

export function headline(n: NewsView, locale: Locale): string {
	const tone: Tone = n.tone > 0 ? 'pos' : n.tone < 0 ? 'neg' : 'neu';
	return headlines[locale][n.kind][tone];
}

export function level(v: number, locale: Locale): string {
	return scale[locale][Math.max(0, Math.min(4, v + 2))];
}

/**
 * A headline reaches the player at this step: its public release, or its early delivery by a
 * fast wire (`newsLead` bars before). Auto-advance pauses on both.
 */
export function newsArrived(s: Pick<GameState, 'step' | 'news' | 'advisor'>): boolean {
	return s.news.some((n) => n.step === s.step || (n.early && n.step - s.advisor.newsLead === s.step));
}
