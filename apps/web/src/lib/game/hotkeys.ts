// Which focused elements keep a key for themselves instead of the game's shortcuts.
// Text entry keeps every key. A select keeps its keys (arrows, type-ahead letters) but not Space;
// a slider or radio keeps only its navigation keys; a checkbox or button keeps none. After clicking
// "BB20" or "VWAP", Space must still advance and P, S, B, A, X must still work (before, the checkbox
// kept every key but Space until focus moved).

const CLICK_INPUTS = new Set(['checkbox', 'button', 'submit', 'reset', 'color', 'file']);
const NAV_INPUTS = new Set(['radio', 'range']);
const NAV_KEYS = new Set(['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Home', 'End', 'PageUp', 'PageDown']);

interface Focusable {
	tagName: string;
	type?: string;
	isContentEditable?: boolean;
}

export function keepsKey(el: Focusable | null, key: string): boolean {
	if (!el) return false;
	if (el.tagName === 'TEXTAREA' || el.isContentEditable) return true;
	if (el.tagName === 'SELECT') return key !== ' ';
	if (el.tagName !== 'INPUT') return false;
	const type = (el.type ?? 'text').toLowerCase();
	if (CLICK_INPUTS.has(type)) return false;
	if (NAV_INPUTS.has(type)) return NAV_KEYS.has(key);
	return true;
}
