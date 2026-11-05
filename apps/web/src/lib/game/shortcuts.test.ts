import { describe, expect, it } from 'vitest';
import { actionFor, bind, DEFAULT_KEYMAP, keyLabel, keyName, sanitizeKeymap, SHORTCUT_ACTIONS } from './shortcuts.ts';

describe('keyName', () => {
	it('names letters case-insensitively and keeps Shift only on named keys', () => {
		expect(keyName({ key: 'B', shiftKey: true })).toBe('b');
		expect(keyName({ key: '?', shiftKey: true })).toBe('?');
		expect(keyName({ key: ' ' })).toBe('Space');
		expect(keyName({ key: ' ', shiftKey: true })).toBe('Shift+Space');
		expect(keyName({ key: 'F2' })).toBe('F2');
		expect(keyName({ key: 'Shift', shiftKey: true })).toBeNull();
	});
	it('labels keys for the help overlay and the settings', () => {
		expect(keyLabel('Shift+Space')).toBe('Shift + Space');
		expect(keyLabel('b')).toBe('B');
		expect(keyLabel('?')).toBe('?');
	});
});

describe('defaults', () => {
	it('bind every action to its own key, including the option chain and the journal', () => {
		const keys = SHORTCUT_ACTIONS.map((a) => DEFAULT_KEYMAP[a]);
		expect(new Set(keys).size).toBe(keys.length);
		expect(actionFor(DEFAULT_KEYMAP, 'o')).toBe('options');
		expect(actionFor(DEFAULT_KEYMAP, 'j')).toBe('journal');
		expect(actionFor(DEFAULT_KEYMAP, 'Shift+Space')).toBe('advance5');
		expect(actionFor(DEFAULT_KEYMAP, 'z')).toBeNull();
	});
});

describe('bind', () => {
	it('remaps an action to a free key', () => {
		const res = bind(DEFAULT_KEYMAP, 'buy', 'k');
		expect(res.ok && res.map.buy).toBe('k');
		expect(res.ok && actionFor(res.map, 'b')).toBeNull();
	});
	it('refuses a key another action uses, naming it', () => {
		expect(bind(DEFAULT_KEYMAP, 'buy', 's')).toEqual({ ok: false, error: 'conflict', key: 's', other: 'sell' });
		expect(bind(DEFAULT_KEYMAP, 'buy', 'b').ok).toBe(true); // its own key is not a conflict
	});
	it('never lets advance (or anything) take Enter, which submits orders', () => {
		expect(bind(DEFAULT_KEYMAP, 'advance', 'Enter')).toEqual({ ok: false, error: 'reserved', key: 'Enter' });
		expect(bind(DEFAULT_KEYMAP, 'journal', 'Escape').ok).toBe(false);
		expect(bind(DEFAULT_KEYMAP, 'journal', 'ArrowLeft').ok).toBe(false); // the chart's candle focus
	});
});

describe('sanitizeKeymap', () => {
	it('fills missing actions with defaults and drops unknown ones', () => {
		expect(sanitizeKeymap({ buy: 'k', bogus: 'q' })).toEqual({ ...DEFAULT_KEYMAP, buy: 'k' });
		expect(sanitizeKeymap(null)).toEqual(DEFAULT_KEYMAP);
		expect(sanitizeKeymap({ buy: 42 })).toEqual(DEFAULT_KEYMAP);
	});
	it('falls back to the defaults for a map with a duplicate or reserved key', () => {
		expect(sanitizeKeymap({ buy: 's' })).toEqual(DEFAULT_KEYMAP);
		expect(sanitizeKeymap({ advance: 'Enter' })).toEqual(DEFAULT_KEYMAP);
	});
});
