import { describe, expect, it } from 'vitest';
import { inDanger, moodFor, StingTracker, stingFor } from './audioRules.ts';
import { ev, game } from './fixtures.test-util.ts';
import type { GameState } from '#lib/engine/types.ts';

const account = (mark: string, liq: string | null, equity = '10000') =>
	({ mark, liquidationPrice: liq, equity }) as GameState['account'];

function active(extra: Partial<GameState> = {}): GameState {
	return game([], { status: 'active', account: account('100', null), ...extra });
}

describe('moodFor', () => {
	it('plays the menu theme outside the terminal (menu, briefing, debrief)', () => {
		expect(moodFor('menu', null)).toBe('menu');
		expect(moodFor('briefing', active())).toBe('menu');
		expect(moodFor('tutorial', active())).toBe('menu');
		expect(moodFor('debrief', active())).toBe('menu');
	});

	it('plays the game theme while trading calmly', () => {
		expect(moodFor('terminal', active())).toBe('game');
	});

	it('turns tense near liquidation, but not when far from it', () => {
		const pos = { qty: '10', entry: '100', margin: '100' };
		expect(moodFor('terminal', active({ position: pos, account: account('100', '96') }))).toBe('tense');
		expect(moodFor('terminal', active({ position: pos, account: account('100', '80') }))).toBe('game');
	});

	it('needs the danger to clear by a margin before calming down (no flip-flop at the threshold)', () => {
		const pos = { qty: '10', entry: '100', margin: '100' };
		const at = (liq: string) => active({ position: pos, account: account('100', liq) });
		expect(moodFor('terminal', at('94'), 'game')).toBe('game'); // 6% away: calm
		expect(moodFor('terminal', at('94'), 'tense')).toBe('tense'); // still within 8% after being tense
		expect(moodFor('terminal', at('90'), 'tense')).toBe('game');
	});

	it('turns tense when written options eat into the maintenance margin', () => {
		const options = { ...active().options, maintenanceMargin: '6000' };
		expect(inDanger(active({ options }))).toBe(true);
	});

	it('stays tense for a few bars after a margin call', () => {
		const g = active({ step: 20, events: [ev({ type: 'margin_call', step: 19 })] });
		expect(inDanger(g)).toBe(true);
		expect(inDanger({ ...g, step: 30 })).toBe(false);
	});

	it('is never tense once the run is over', () => {
		const pos = { qty: '10', entry: '100', margin: '100' };
		expect(inDanger(game([], { status: 'finished', position: pos, account: account('100', '99') }))).toBe(false);
	});
});

describe('stingFor', () => {
	it('plays win for a profitable close and loss for a losing one', () => {
		expect(stingFor([ev({ seq: 5, type: 'fill', pnl: '12.5' })], 4)).toBe('win');
		expect(stingFor([ev({ seq: 5, type: 'fill', pnl: '-3' })], 4)).toBe('loss');
	});

	it('nets several closes in one advance and counts option settlements', () => {
		const events = [ev({ seq: 5, type: 'fill', pnl: '-3' }), ev({ seq: 6, type: 'option_settlement', series: 2, pnl: '8' })];
		expect(stingFor(events, 4)).toBe('win');
	});

	it('always plays loss on a liquidation', () => {
		expect(stingFor([ev({ seq: 5, type: 'fill', pnl: '50' }), ev({ seq: 6, type: 'liquidation', pnl: '0' })], 4)).toBe('loss');
	});

	it('is silent for entries, funding and events already heard', () => {
		expect(stingFor([ev({ seq: 5, type: 'fill', pnl: '0' }), ev({ seq: 6, type: 'funding', pnl: '0', amount: '-1' })], 4)).toBeNull();
		expect(stingFor([ev({ seq: 3, type: 'fill', pnl: '9' })], 4)).toBeNull();
	});
});

describe('StingTracker', () => {
	it('never replays the history of a resumed or new run, then plays what follows', () => {
		const tr = new StingTracker();
		const history = [ev({ seq: 1, type: 'fill', pnl: '0' }), ev({ seq: 2, type: 'fill', pnl: '40' })];
		expect(tr.observe('run-a', history)).toBeNull();
		expect(tr.observe('run-a', [...history, ev({ seq: 3, type: 'fill', pnl: '-5' })])).toBe('loss');
		expect(tr.observe('run-a', [...history, ev({ seq: 3, type: 'fill', pnl: '-5' })])).toBeNull();
		expect(tr.observe('run-b', [ev({ seq: 1, type: 'fill', pnl: '9' })])).toBeNull();
	});
});
