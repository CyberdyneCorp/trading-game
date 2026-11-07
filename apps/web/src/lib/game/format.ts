// Display formatting of engine decimal strings. Display only: never feed results back as state.

import type { Decimal } from '#lib/engine/types.ts';
import type { Locale } from './i18n.svelte';

export const num = (d: Decimal | null | undefined): number => (d == null ? NaN : Number(d));

function formatter(locale: Locale, digits: number, signed = false): Intl.NumberFormat {
	return new Intl.NumberFormat(locale, {
		minimumFractionDigits: digits,
		maximumFractionDigits: digits,
		// 'negative' never prints "-0,00" for a value that rounds to zero (e.g. a far put's delta).
		signDisplay: signed ? 'exceptZero' : 'negative'
	});
}

export function fmtPrice(d: Decimal | null | undefined, locale: Locale): string {
	const v = num(d);
	return Number.isFinite(v) ? formatter(locale, 2).format(v) : '—';
}

export function fmtQty(d: Decimal | null | undefined, locale: Locale): string {
	const v = num(d);
	return Number.isFinite(v) ? formatter(locale, 1).format(v) : '—';
}

export function fmtMoney(d: Decimal | number | null | undefined, locale: Locale, signed = false): string {
	const v = typeof d === 'number' ? d : num(d);
	return Number.isFinite(v) ? `${formatter(locale, 2, signed).format(v)} cr` : '—';
}

/** A plain number in the locale (indicator values, Greeks, R multiples): "63,0" in PT-BR. */
export function fmtNum(v: number | null | undefined, locale: Locale, digits = 2, signed = false): string {
	return v != null && Number.isFinite(v) ? formatter(locale, digits, signed).format(v) : '—';
}

/**
 * Reads a number typed by the player in either locale: "95,56" (PT-BR keyboards give a comma) and
 * "95.56" both mean 95.56. With both separators the last one is the decimal mark. NaN when invalid.
 */
export function parseDecimal(input: string | number | null | undefined): number {
	if (typeof input === 'number') return input;
	const s = (input ?? '').replace(/\s/g, '');
	const decimal = s.lastIndexOf(',') > s.lastIndexOf('.') ? ',' : '.';
	const plain = s.split(decimal === ',' ? '.' : ',').join('').replace(decimal, '.');
	return /^[+-]?(\d+\.?\d*|\.\d+)$/.test(plain) ? Number(plain) : NaN;
}

/**
 * Reads a credit amount typed by the player. Like parseDecimal, except that the locale's thousands
 * separator ("." in PT-BR, "," in English) followed only by groups of exactly three digits groups
 * thousands: "1.000" in PT-BR is one thousand credits, not 1. NaN when invalid.
 */
export function parseAmount(input: string | number | null | undefined, locale: Locale): number {
	if (typeof input === 'number') return input;
	const s = (input ?? '').replace(/\s/g, '');
	const grouped = locale === 'pt-BR' ? /^[+-]?\d{1,3}(\.\d{3})+$/ : /^[+-]?\d{1,3}(,\d{3})+$/;
	return grouped.test(s) ? Number(s.replace(/[.,]/g, '')) : parseDecimal(s);
}

export function fmtPct(fraction: number, locale: Locale, digits = 2, signed = false): string {
	if (!Number.isFinite(fraction)) return '—';
	return `${formatter(locale, digits, signed).format(fraction * 100)}%`;
}

/** Sign marker so gains/losses never rely on color alone. */
export function signMark(v: number): string {
	if (v > 0) return '▲';
	if (v < 0) return '▼';
	return '•';
}

/** Floors a quantity to the lot size and returns the engine's decimal string form. */
export function qtyToLot(qty: number, lot: number): string {
	if (!Number.isFinite(qty) || qty <= 0) return '0';
	const lots = Math.floor(qty / lot + 1e-9);
	if (lots === 0) return '0';
	const decimals = Math.max(0, Math.round(-Math.log10(lot)));
	return (lots * lot).toFixed(decimals);
}

/** Rounds a price to the tick and returns the engine's decimal string form. */
export function priceToTick(price: number, tick: number): string {
	if (!Number.isFinite(price) || price <= 0) return '';
	const ticks = Math.round(price / tick);
	const decimals = Math.max(0, Math.round(-Math.log10(tick)));
	return (ticks * tick).toFixed(decimals);
}

/** A price on the tick as the player would type it in an input: "99,96" in PT-BR, "99.96" in English. */
export function priceInputText(price: number, tick: number, locale: Locale): string {
	const s = priceToTick(price, tick);
	return locale === 'pt-BR' ? s.replace('.', ',') : s;
}

/** A number as the player would type it in an input: "6,85" in PT-BR, "6.85" in English. */
export function inputText(v: number, locale: Locale): string {
	const s = String(v);
	return locale === 'pt-BR' ? s.replace('.', ',') : s;
}

/** A quantity on the lot as the player would type it in an input: "16,6" in PT-BR, "16.6" in English. */
export function qtyInputText(qty: number, lot: number, locale: Locale): string {
	const s = qtyToLot(qty, lot);
	return locale === 'pt-BR' ? s.replace('.', ',') : s;
}

/** Plan dialog headline: "▲ LONG 49,9 · 3x", or "▼ C 100 · exp 96 × 20" for an option. */
export function planHeadline(
	d: { side: 'buy' | 'sell'; qty: Decimal; leverage: number },
	optionLabel: string | null,
	locale: Locale
): string {
	const up = d.side === 'buy';
	const q = num(d.qty);
	if (optionLabel) return `${up ? '▲' : '▼'} ${optionLabel} × ${fmtNum(q, locale, Number.isInteger(q) ? 0 : 1)}`;
	return `${up ? '▲ LONG' : '▼ SHORT'} ${fmtQty(d.qty, locale)} · ${d.leverage}x`;
}
