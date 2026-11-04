// Which focused elements keep a key for themselves instead of the game's shortcuts.
// Text entry keeps every key. Toggles and pickers (checkbox, radio, slider, select) keep their own
// keys but not Space: after clicking "BB20" or choosing an oscillator, Space must still advance
// (before, it toggled the checkbox or opened the list and the market never moved).

const NON_TEXT_INPUTS = new Set(['checkbox', 'radio', 'range', 'button', 'submit', 'reset', 'color', 'file']);

interface Focusable {
	tagName: string;
	type?: string;
	isContentEditable?: boolean;
}

export function keepsKey(el: Focusable | null, key: string): boolean {
	if (!el) return false;
	if (el.tagName === 'TEXTAREA' || el.isContentEditable) return true;
	const toggle = el.tagName === 'SELECT' || (el.tagName === 'INPUT' && NON_TEXT_INPUTS.has((el.type ?? 'text').toLowerCase()));
	if (toggle) return key !== ' ';
	return el.tagName === 'INPUT';
}
