// Remappable keyboard shortcuts (spec web-client "Keyboard operation"): the actions, their default
// keys, how a key event is named, and the checks a new binding must pass. Pure and unit-tested; the
// persisted map lives in keymap.svelte.ts.

export const SHORTCUT_ACTIONS = [
	'advance',
	'advance5',
	'buy',
	'sell',
	'close',
	'patterns',
	'auto',
	'mute',
	'options',
	'journal',
	'help'
] as const;

export type ShortcutAction = (typeof SHORTCUT_ACTIONS)[number];
export type Keymap = Record<ShortcutAction, string>;

export const DEFAULT_KEYMAP: Readonly<Keymap> = {
	advance: 'Space',
	advance5: 'Shift+Space',
	buy: 'b',
	sell: 's',
	close: 'x',
	patterns: 'p',
	auto: 'a',
	mute: 'm',
	options: 'o',
	journal: 'j',
	help: '?'
};

/**
 * Keys no shortcut may take: Enter submits the focused order button (advance never shares a key with
 * submission), Escape closes dialogs and cancels a chart pick, Tab moves the focus, arrows and
 * Home/End/Page keys move the chart's focused candle. Modifier keys alone are not keys.
 */
const RESERVED = new Set(['Enter', 'Escape', 'Tab', 'Shift+Tab', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End', 'PageUp', 'PageDown']);
const MODIFIERS = new Set(['Shift', 'Control', 'Alt', 'Meta', 'CapsLock', 'Dead', 'Unidentified', 'Process']);

interface KeyLike {
	key: string;
	shiftKey?: boolean;
}

/**
 * The name of a key press as stored in the map. A printable character is its lower-case self (Shift
 * only changes which character it is, so "B" and "b" are the same key, and "?" is "?"); a named key
 * such as Space keeps a "Shift+" prefix, so Space and Shift+Space are two keys. Null for a modifier.
 */
export function keyName(e: KeyLike): string | null {
	if (MODIFIERS.has(e.key)) return null;
	const name = e.key === ' ' ? 'Space' : e.key.length === 1 ? e.key.toLowerCase() : e.key;
	return e.shiftKey && name.length > 1 ? `Shift+${name}` : name;
}

/** How a stored key is shown: "Space", "Shift + Space", "B", "?". */
export function keyLabel(key: string): string {
	return key.split('+').map((p) => (p.length === 1 ? p.toUpperCase() : p)).join(' + ');
}

/** The action bound to a key, if any. */
export function actionFor(map: Keymap, key: string | null): ShortcutAction | null {
	if (!key) return null;
	return SHORTCUT_ACTIONS.find((a) => map[a] === key) ?? null;
}

export type BindResult =
	| { ok: true; map: Keymap }
	| { ok: false; error: 'reserved'; key: string }
	| { ok: false; error: 'conflict'; key: string; other: ShortcutAction };

/** Binds `key` to `action`, refusing a reserved key or one another action already uses. */
export function bind(map: Keymap, action: ShortcutAction, key: string): BindResult {
	if (RESERVED.has(key)) return { ok: false, error: 'reserved', key };
	const other = actionFor(map, key);
	if (other && other !== action) return { ok: false, error: 'conflict', key, other };
	return { ok: true, map: { ...map, [action]: key } };
}

/**
 * A stored map read back: unknown actions are dropped, missing or invalid ones get their default, and
 * if the result still binds one key twice (or a reserved key) the defaults are used instead.
 */
export function sanitizeKeymap(raw: unknown): Keymap {
	if (!raw || typeof raw !== 'object') return { ...DEFAULT_KEYMAP };
	const src = raw as Record<string, unknown>;
	const map = { ...DEFAULT_KEYMAP };
	for (const a of SHORTCUT_ACTIONS) {
		const v = src[a];
		if (typeof v === 'string' && v.length > 0) map[a] = v;
	}
	const keys = SHORTCUT_ACTIONS.map((a) => map[a]);
	const valid = new Set(keys).size === keys.length && keys.every((k) => !RESERVED.has(k));
	return valid ? map : { ...DEFAULT_KEYMAP };
}
