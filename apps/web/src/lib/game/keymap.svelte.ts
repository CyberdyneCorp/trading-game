// The player's shortcut map, persisted per viewer (localStorage is fine: a convenience, like the
// display settings). The rules live in shortcuts.ts.

import { bind, DEFAULT_KEYMAP, sanitizeKeymap, type BindResult, type Keymap, type ShortcutAction } from './shortcuts.ts';

const KEY = 'tradelab.keymap.v1';

class KeymapStore {
	map = $state<Keymap>({ ...DEFAULT_KEYMAP });
	/** True while the settings wait for a new key: game shortcuts stay off meanwhile. */
	capturing = $state(false);

	load(): void {
		try {
			this.map = sanitizeKeymap(JSON.parse(localStorage.getItem(KEY) ?? 'null'));
		} catch {
			this.map = { ...DEFAULT_KEYMAP };
		}
	}

	set(action: ShortcutAction, key: string): BindResult {
		const res = bind(this.map, action, key);
		if (res.ok) {
			this.map = res.map;
			this.save();
		}
		return res;
	}

	reset(): void {
		this.map = { ...DEFAULT_KEYMAP };
		this.save();
	}

	private save(): void {
		try {
			localStorage.setItem(KEY, JSON.stringify(this.map));
		} catch {
			// ignore unavailable storage: the map still applies until the page is closed
		}
	}
}

export const keymap = new KeymapStore();
