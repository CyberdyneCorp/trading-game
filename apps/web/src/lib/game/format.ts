// Display formatting of engine decimal strings. Display only: never feed results back as state.

import type { Decimal } from '#lib/engine/types.ts';
import type { Locale } from './i18n.svelte';

export const num = (d: Decimal | null | undefined): number => (d == null ? NaN : Number(d));

function formatter(locale: Locale, digits: number, signed = false): Intl.NumberFormat {
	return new Intl.NumberFormat(locale, {
		minimumFractionDigits: digits,
		maximumFractionDigits: digits,
		signDisplay: signed ? 'exceptZero' : 'auto'
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
