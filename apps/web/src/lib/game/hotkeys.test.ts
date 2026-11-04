import { describe, expect, it } from 'vitest';
import { keepsKey } from './hotkeys.ts';

describe('keyboard shortcuts vs focused elements', () => {
	it('regression: Space advances while an indicator checkbox or the oscillator select has focus', () => {
		expect(keepsKey({ tagName: 'INPUT', type: 'checkbox' }, ' ')).toBe(false);
		expect(keepsKey({ tagName: 'SELECT' }, ' ')).toBe(false);
		expect(keepsKey({ tagName: 'INPUT', type: 'range' }, ' ')).toBe(false);
		expect(keepsKey({ tagName: 'INPUT', type: 'radio' }, ' ')).toBe(false);
	});

	it('toggles and pickers keep their other keys (arrows, type-ahead letters)', () => {
		expect(keepsKey({ tagName: 'SELECT' }, 'a')).toBe(true);
		expect(keepsKey({ tagName: 'INPUT', type: 'range' }, 'ArrowLeft')).toBe(true);
	});

	it('regression: letter shortcuts work after clicking an indicator checkbox', () => {
		for (const key of ['p', 's', 'b', 'a', 'x', 'P']) {
			expect(keepsKey({ tagName: 'INPUT', type: 'checkbox' }, key)).toBe(false);
		}
		expect(keepsKey({ tagName: 'INPUT', type: 'radio' }, 'p')).toBe(false);
		expect(keepsKey({ tagName: 'INPUT', type: 'range' }, 'b')).toBe(false);
		expect(keepsKey({ tagName: 'INPUT', type: 'radio' }, 'ArrowDown')).toBe(true);
	});

	it('text entry keeps every key, Space included', () => {
		expect(keepsKey({ tagName: 'INPUT', type: 'text' }, ' ')).toBe(true);
		expect(keepsKey({ tagName: 'INPUT' }, ' ')).toBe(true);
		expect(keepsKey({ tagName: 'INPUT', type: 'number' }, 'b')).toBe(true);
		expect(keepsKey({ tagName: 'TEXTAREA' }, 'x')).toBe(true);
		expect(keepsKey({ tagName: 'DIV', isContentEditable: true }, ' ')).toBe(true);
	});

	it('buttons and the page do not keep shortcuts', () => {
		expect(keepsKey({ tagName: 'BUTTON' }, ' ')).toBe(false);
		expect(keepsKey(null, ' ')).toBe(false);
	});
});
