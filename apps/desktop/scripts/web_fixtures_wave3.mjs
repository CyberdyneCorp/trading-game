// Web parity fixtures of add-practice-and-advanced-strategies wave 3 (practice formats): the web's
// prng.ts and drills.ts (drills.json: the first 100 mulberry32 outputs of 3 seeds and the generated
// sessions with their answers and texts), daily.ts (daily.json: 400 dates), guessRound.ts (guess.json:
// round picks and scores) and coachReview.ts (coach texts and the mistake comparison). The desktop
// ports replay every case (task W3-D2, W3-D3).

function cases() {
	const list = [];
	const add = (fn, args, out) => list.push({ fn, args, out: out === undefined ? null : out });
	return { list, add };
}

const LOCALES = ['pt-BR', 'en'];

export function drillsFixture(prng, drills, content) {
	const { list, add } = cases();
	for (const text of ['', 'a', 'drill-v1:2026-11-09:0']) add('fnv1a32', [text], prng.fnv1a32(text));
	for (const seed of [1, 42, 3735928559]) {
		const next = prng.mulberry32(seed);
		add('mulberry32', [seed, 100], Array.from({ length: 100 }, () => next()));
	}
	const empty = { lessonReviews: {}, drillReviews: {}, history: [] };
	const inputs = [
		{ date: '2026-11-09', now: Date.UTC(2026, 10, 9, 13), session: 0, ...empty },
		{ date: '2026-11-09', now: Date.UTC(2026, 10, 9, 13), session: 1, ...empty },
		{ date: '2027-02-28', now: Date.UTC(2027, 1, 28, 13), session: 0, ...empty, lessonReviews: { 'options-payoff': { box: 1, due: '2027-02-27' } } },
		{
			date: '2026-12-01',
			now: Date.UTC(2026, 11, 1, 13),
			session: 2,
			lessonReviews: {},
			drillReviews: { trail: { box: 2, due: Date.UTC(2026, 10, 30) }, funding: { box: 3, due: Date.UTC(2026, 11, 20) } },
			history: [{ date: '2026-11-30', items: 6, firstTry: 2, skills: { calendar: { n: 2, right: 0 }, sizing: { n: 1, right: 1 } } }]
		}
	];
	for (const input of inputs) {
		const items = drills.buildSession(input);
		const texts = items.map((it) => {
			const t = content.DRILL_TEMPLATES.find((x) => x.id === it.template);
			return Object.fromEntries(LOCALES.map((l) => [l, drills.itemText(it, t, l)]));
		});
		add('buildSession', [input], { items, texts });
	}
	// Every template over 20 seeds: parameters on the grid and the answer of the lesson checks.
	for (const t of content.DRILL_TEMPLATES) {
		for (let s = 1; s <= 20; s++) add('makeItem', [t.id, s * 7919], drills.makeItem(t, prng.mulberry32(s * 7919)));
	}
	for (const text of ['12,5', ' -3.25 ', 'abc', '1.234']) add('parseAnswer', [text], drills.parseAnswer(text));
	return { helper: 'drills.ts', cases: list };
}

export function dailyFixture(daily) {
	const { list, add } = cases();
	const start = Date.UTC(2025, 11, 20);
	for (let i = 0; i < 400; i++) {
		const d = new Date(start + i * 86_400_000);
		const date = `${d.getUTCFullYear()}-${String(d.getUTCMonth() + 1).padStart(2, '0')}-${String(d.getUTCDate()).padStart(2, '0')}`;
		add('dailyChallenge', [date], daily.dailyChallenge(date));
	}
	add('pool', [], [...daily.DAILY_POOL_V1]);
	return { helper: 'daily.ts', cases: list };
}

export function guessFixture(guess) {
	const { list, add } = cases();
	const markets = [
		{ id: 'r01-trend', totalBars: 160, warmupBars: 60 },
		{ id: 'r02-range', totalBars: 160, warmupBars: 60 },
		{ id: 'r03-breakout', totalBars: 150, warmupBars: 60 },
		{ id: 'r04-event', totalBars: 160, warmupBars: 60 }
	];
	for (let i = 0; i < 20; i++) add('pickRound', ['2026-11-09', i], guess.pickRound(markets, '2026-11-09', i));
	const facts = [
		{ close: '100', atr: '2', higher: true },
		{ close: '100', atr: '2', higher: false },
		{ close: '90.58', atr: '0.95', higher: false }
	];
	const commitments = [
		{ decision: 'long', p: 80, invalidation: '98', target: '104' },
		{ decision: 'long', p: 70, invalidation: '89.67', target: '92.39' },
		{ decision: 'long', p: 60, invalidation: '99.01', target: '104' },
		{ decision: 'long', p: 60, invalidation: '99', target: '101.49' },
		{ decision: 'long', p: 55, invalidation: '101', target: '104' },
		{ decision: 'short', p: 30, invalidation: '102', target: '97' },
		{ decision: 'short', p: 50, invalidation: '94', target: '120' },
		{ decision: 'none', p: 50 },
		{ decision: 'none', p: 70 },
		{ decision: 'none', p: 5 }
	];
	for (const c of commitments) for (const f of facts) add('scoreRound', [c, f], guess.scoreRound(c, f));
	const bars = [
		[{ high: '104.5', low: '99' }],
		[{ high: '101', low: '97.99' }],
		[{ high: '104.5', low: '97' }],
		[{ high: '101', low: '99' }]
	];
	for (const b of bars) add('firstHit', [commitments[0], b], guess.firstHit(commitments[0], b));
	add('calibrationTable', [[{ p: 70, higher: true }, { p: 75, higher: false }, { p: 95, higher: true }, { p: 20, higher: false }]], guess.calibrationTable([
		{ p: 70, higher: true },
		{ p: 75, higher: false },
		{ p: 95, higher: true },
		{ p: 20, higher: false }
	]));
	return { helper: 'guessRound.ts', cases: list };
}

export function coachFixture(coach, i18n) {
	const { list, add } = cases();
	const m = (code, evidence, step = 84, tone = 'warn') => ({ step, code, tone, evidence, lesson: '', glossary: '' });
	const moments = [
		m('entry_without_stop', { qty: '10', entry: '101.44', close: '100.63' }, 61),
		m('stop_moved_away', { from: '98', to: '97', entry: '100' }),
		m('added_against_structure', { qty: '10', close: '99', sma: '99.85', level: '99.4' }),
		m('added_against_structure', { qty: '10', close: '101', sma: '100', level: '100.6' }),
		m('risk_above_budget', { qty: '201.4', stop: '97.07', risk: '298.072', budget: '200' }, 83),
		m('held_into_release_unhedged', { qty: '10', releaseStep: 35 }, 34),
		m('held_into_release_unhedged', { releaseStep: 95, limit: 'vega', limitBp: 150, valueBp: 212 }, 94),
		m('revenge_trade', { trade: 1, exitStep: 83, loss: '112.81024645', sizeRatioBp: 20000, planned: false }, 83),
		m('revenge_trade', { trade: 2, exitStep: 90, loss: '20', sizeRatioBp: 15000, planned: true }, 91),
		m('fomo_entry', { closes: 5, extensionAtr: 300, close: '105', sma: '101.2', stop: '99.59' }),
		m('fomo_entry', { closes: 4, extensionAtr: 250, close: '105', sma: '101.2', stop: '0' }),
		m('averaging_down', { avgBefore: '100', avgAfter: '99.33333333', stop: '95', loss: '10', riskBefore: '50', riskAfter: '129.9999999', planRisk: '60', planned: false }),
		m('averaging_down', { avgBefore: '100', avgAfter: '99.5', stop: '0', loss: '10', riskBefore: '0', riskAfter: '0', planRisk: '0', planned: false }),
		m('risk_limit_exceeded', { limit: 'delta', limitBp: 1500, valueBp: 1637 }, 91),
		m('cut_at_invalidation', { price: '97.97', loss: '112.81024645', plannedRisk: '99.91' }, 83, 'good'),
		m('stop_to_breakeven_after_1r', { from: '97.67', to: '98.99', entry: '98.96', plannedRisk: '99.88', profitRBp: 16463 }, 85, 'good'),
		m('partial_at_target', { qty: '37.7', price: '103.73', entry: '98.96', plannedRisk: '99.88', profitRBp: 36057 }, 76, 'good'),
		m('stayed_flat_before_release', { releaseStep: 88 }, 87, 'good')
	];
	for (const L of LOCALES) {
		i18n.i18n.locale = L;
		for (const x of moments) add('momentText', [x, L], { key: coach.momentKey(x), text: coach.momentText(x, L) });
	}
	i18n.i18n.locale = 'pt-BR';
	const f = (code, step) => ({ code, step, tone: 'warn' });
	const retries = [
		['averaging_down', 90, { moments: [], findings: [] }],
		['averaging_down', 90, { moments: [], findings: [f('averaging_down', 95)] }],
		['averaging_down', 90, { moments: [], findings: [f('averaging_down', 96)] }],
		['no_stop', 70, { moments: [m('entry_without_stop', {}, 71)], findings: [] }],
		['oversized', 70, { moments: [m('risk_above_budget', {}, 70)], findings: [] }],
		['revenge_trade', 83, { moments: [m('revenge_trade', {}, 89)], findings: [] }]
	];
	for (const [code, step, retry] of retries) add('mistakeOutcome', [code, step, retry], coach.mistakeOutcome(code, step, retry));
	for (const code of ['revenge_trade', 'oversized', 'gap_slippage']) add('mistakeName', [code], coach.mistakeName(code));
	return { helper: 'coachReview.ts', cases: list };
}
