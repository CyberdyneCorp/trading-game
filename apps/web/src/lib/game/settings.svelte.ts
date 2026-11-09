// Per-viewer display settings (localStorage is fine here: conveniences only).

import { i18n, type Locale } from './i18n.svelte';

const KEY = 'tradelab.settings.v1';

interface Stored {
	crt: boolean;
	colorblind: boolean;
	locale: Locale;
	/** Daily drills: show the soft two-minute clock (absent = shown). */
	drillTimer?: boolean;
}

class Settings {
	crt = $state(true);
	colorblind = $state(false);
	/** The drills' soft two-minute clock; off = no clock at all (accessibility, design D15). */
	drillTimer = $state(true);

	load(): void {
		const reduced = typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;
		this.crt = !reduced;
		try {
			const s = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Stored | null;
			if (s) {
				this.crt = s.crt && !reduced;
				this.colorblind = s.colorblind;
				this.drillTimer = s.drillTimer !== false;
				i18n.locale = s.locale;
			}
		} catch {
			// ignore unavailable storage
		}
		this.apply();
	}

	save(): void {
		this.apply();
		try {
			localStorage.setItem(KEY, JSON.stringify({ crt: this.crt, colorblind: this.colorblind, locale: i18n.locale, drillTimer: this.drillTimer }));
		} catch {
			// ignore unavailable storage
		}
	}

	private apply(): void {
		document.documentElement.dataset.palette = this.colorblind ? 'cb' : 'default';
		document.documentElement.lang = i18n.locale;
	}
}

export const settings = new Settings();
