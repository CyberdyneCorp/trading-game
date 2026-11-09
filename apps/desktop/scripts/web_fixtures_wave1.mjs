// Web parity fixtures of wave 1 of add-practice-and-advanced-strategies: orderRules.ts and stops.ts
// trailTrigger (time in force, OCO links, the trailing stop), the two-expiry, limit, roll and hedge
// helpers of strategies.ts, the submissions of combos.ts, heatmap.ts and portfolio.ts. Same format as
// web_fixtures_lib.mjs; replayed by tests/viewmodel/game/test_wave1_parity.cpp and
// tests/viewmodel/game/test_options_parity.cpp.

function cases() {
	const list = [];
	const add = (fn, args, out) => list.push({ fn, args, out: out === undefined ? null : out });
	return { list, add };
}

const order = (id, extra = {}) => ({
	id,
	type: 'limit',
	side: 'buy',
	qty: '1.00000000',
	price: '100.00000000',
	parentId: 0,
	linkGroup: 0,
	tif: 'gtc',
	tifBars: 0,
	expireStep: 0,
	trailDistance: '0.00000000',
	trailRate: '0.0000000000',
	...extra
});

function tifCases(m, add) {
	for (const s of [3600, 900, 86400, 172800, 0, 7200]) add('dayBars', [s], m.dayBars(s));
	for (const g of [
		{ totalBars: 200, step: 60 },
		{ totalBars: 200, step: 199 },
		{ totalBars: 200, step: 250 }
	]) {
		add('barsLeft', [g], m.barsLeft(g));
	}
	for (const c of m.TIF_CHOICES) {
		for (const type of ['market', 'limit', 'stop_market', 'take_profit', 'trailing_stop']) add('tifBlock', [c, type], m.tifBlock(c, type));
		for (const [bars, step, left] of [
			[3, 3600, 100],
			[0, 3600, 100],
			[2.7, 900, 100],
			[500, 3600, 100],
			[5, 3600, 5],
			[3, 3600, 0],
			[3, 900, 40]
		]) {
			add('tifOrder', [c, bars, step, left], m.tifOrder(c, bars, step, left));
		}
	}
	for (const o of [
		order(1),
		order(2, { tif: 'bars', tifBars: 3, expireStep: 63 }),
		order(3, { tif: 'ioc', expireStep: 61 }),
		order(4, { tif: 'fok', expireStep: 0 }),
		{ id: 5 }
	]) {
		add('tifCell', [o], m.tifCell(o));
	}
}

function linkCases(m, add) {
	const orders = [order(1), order(2, { parentId: 1 }), order(3, { linkGroup: 3 }), order(4, { linkGroup: 3 }), order(5), order(6, { linkGroup: 9 })];
	for (const [a, b] of [
		[0, 0],
		[0, 4],
		[0, 1],
		[1, 4],
		[2, 4],
		[4, 2],
		[0, 5]
	]) {
		add('linkProblem', [orders[a], orders[b]], m.linkProblem(orders[a], orders[b]));
	}
	for (const o of orders) add('linkMark', [o, orders], m.linkMark(o, orders));
}

function trailCases(m, stops, add) {
	const closes = [100, 101.5, null, 99.2, 103.4, 102, 103.4, 98.7];
	for (const [placed, exit] of [
		[0, 'sell'],
		[0, 'buy'],
		[3, 'sell'],
		[3, 'buy'],
		[7, 'sell'],
		[9, 'buy']
	]) {
		add('bestClose', [closes, placed, exit], m.bestClose(closes.map((c) => (c == null ? NaN : c)), placed, exit));
	}
	for (const [exit, close, trail, tick] of [
		['sell', '103.40000000', { distance: '1.50000000' }, '0.01000000'],
		['buy', '98.70000000', { distance: '1.50000000' }, '0.01000000'],
		['sell', '103.40000000', { rate: '0.020000' }, '0.01000000'],
		['buy', '98.70000000', { rate: '0.020000' }, '0.01000000'],
		['sell', '0.03000000', { distance: '0.05000000' }, '0.01000000'],
		['sell', '2534.57000000', { rate: '0.012345' }, '0.50000000'],
		['buy', '2534.57000000', { rate: '0.012345' }, '0.50000000'],
		['sell', '101.37', { distance: '0.1' }, '0.05'],
		['buy', '0.01000000', { distance: '0.00000000' }, '0.01000000']
	]) {
		add('trailTrigger', [exit, close, trail, tick], stops.trailTrigger(exit, close, trail, tick));
	}
	for (const locale of ['pt-BR', 'en']) {
		for (const o of [
			{ trailDistance: '1.50000000', trailRate: '0', price: '101.96000000' },
			{ trailDistance: '0', trailRate: '0.0200000000', price: '101.96000000' },
			{ trailDistance: '0', trailRate: '0.0125000000', price: '2504.50000000' },
			{ trailDistance: '0', trailRate: '0.0050000000', price: '99.10000000' }
		]) {
			add('trailLabel', [o, locale], m.trailLabel(o, locale));
		}
	}
	const toRate = (pct) => stops.trailRateFromPercent(pct);
	const parse = (s) => Number(s.replace(',', '.'));
	for (const [p, tick] of [
		[{ mode: 'price', value: '1.5' }, 0.01],
		[{ mode: 'price', value: '1,234' }, 0.05],
		[{ mode: 'price', value: '0.001' }, 0.01],
		[{ mode: 'price', value: '0' }, 0.01],
		[{ mode: 'pct', value: '2' }, 0.01],
		[{ mode: 'pct', value: '0,5' }, 0.01],
		[{ mode: 'pct', value: '150' }, 0.01]
	]) {
		add('plannedTrailOrder', [p, tick], m.plannedTrailOrder(p, tick, toRate, parse));
	}
}

export function orderRulesFixture(m, stops) {
	const { list, add } = cases();
	tifCases(m, add);
	linkCases(m, add);
	trailCases(m, stops, add);
	return { helper: 'orderRules.ts', cases: list };
}

// ---- strategies.ts (wave 1) ----

const STRIKES_NEAR = [90, 92.5, 95, 97.5, 100, 102.5, 105, 107.5, 110];
const STRIKES_FAR = [90, 95, 100, 105, 110];

export function strategiesWave1Cases(m, add, chain, games) {
	for (const id of m.TEMPLATES) {
		for (const f of [100, 101.2, 96.3, 111, 89]) {
			for (const [n, short] of [
				[1, false],
				[2.5, true]
			]) {
				const near = { step: 100, strikes: STRIKES_NEAR };
				const far = { step: 150, strikes: STRIKES_FAR };
				add('twoExpiryTemplateLegs', [id, near, far, f, n, short], m.twoExpiryTemplateLegs(id, near, far, f, n, short));
			}
		}
	}
	add('twoExpiryTemplateLegs', ['calendar_call', { step: 150, strikes: STRIKES_NEAR }, { step: 100, strikes: STRIKES_FAR }, 100, 1, false], null);
	add('twoExpiryTemplateLegs', ['calendar_call', { step: 100, strikes: [] }, { step: 150, strikes: STRIKES_FAR }, 100, 1, false], null);
	for (const [v, tick] of [
		[1.234, 0.01],
		[1.2351, 0.05],
		[0.004, 0.01],
		[13.46, 0.5],
		[7, 1]
	]) {
		add('premiumText', [v, tick], m.premiumText(v, tick));
	}
	for (const [mid, side, tick] of [
		['1.23500000', 'buy', 0.01],
		['1.23500000', 'sell', 0.01],
		['1.20000000', 'buy', 0.05],
		['1.20000000', 'sell', 0.05],
		['0.00400000', 'buy', 0.01],
		['0.00400000', 'sell', 0.01],
		['13.46000000', 'sell', 0.5]
	]) {
		add('defaultLimit', [{ mid }, side, tick], m.defaultLimit({ mid }, side, tick));
	}
	const two = [
		{ kind: 'call', side: 'sell', strike: 100, qty: 1, expiry: 100 },
		{ kind: 'call', side: 'buy', strike: 100, qty: 1, expiry: 150, limit: 4.237 },
		{ kind: 'perp', side: 'buy', strike: null, qty: 2 }
	];
	const three = [...two.slice(0, 2), { kind: 'put', side: 'buy', strike: 95, qty: 1, expiry: 80 }];
	for (const legs of [two, three, [{ kind: 'put', side: 'buy', strike: 95, qty: 1 }, ...two.slice(0, 1)]]) {
		add('legsProblem', [legs, 100], m.legsProblem(legs, 100));
		add('legExpiries', [legs, 100], m.legExpiries(legs, 100));
		add('submissionOrder', [legs], m.submissionOrder(legs));
		for (const g of games) add('orderLegs', [g, 100, legs], m.orderLegs(g, 100, legs));
	}
	const limitOrders = [
		{ series: 9, side: 'buy', qty: '1.0', premium: '4.25' },
		{ series: 0, side: 'buy', qty: '2.5' },
		{ series: 12, side: 'sell', qty: '1.0', premium: '1.10' },
		{ series: 14, side: 'sell', qty: '1.0' }
	];
	for (const tif of [undefined, { tif: 'gtc' }, { tif: 'ioc' }, { tif: 'fok' }, { tif: 'bars', tifBars: 3 }]) {
		add('comboLegs', [limitOrders, tif ?? null], m.comboLegs(limitOrders, tif));
	}
	add('whatifLegs', [limitOrders, null], m.whatifLegs(limitOrders, null));
	add('whatifLegs', [limitOrders, { qty: '4.80000000', entry: '99.50000000' }], m.whatifLegs(limitOrders, { qty: '4.80000000', entry: '99.50000000' }));
	for (const from of [80, 100, 150, 200]) add('rollTargets', [chain, from, []], m.rollTargets(chain, from));
	for (const held of [[100, 150], [100], [80, 200], [150, 100]]) add('rollTargets', [chain, Math.min(...held), held], m.rollTargets(chain, Math.min(...held), held));
	rollCases(m, add, games);
	for (const rules of [{}, { spreadMargin: true }, { spreadMargin: true, calendarMargin: true }]) {
		for (const two of [false, true]) add('marginLabelKey', [rules, two], m.marginLabelKey(rules, two));
	}
	for (const g of games) {
		for (const d of [0, 0.05, 3.27, -3.27, 48.5, -0.15]) add('hedgeOrder', [g, d], m.hedgeOrder(g, d));
	}
}

function rollCases(m, add, games) {
	const id = (expiry, strike, kind) => {
		const exp = [100, 150, 80, 200].indexOf(expiry);
		return exp * STRIKES_NEAR.length * 2 + STRIKES_NEAR.indexOf(strike) * 2 + (kind === 'call' ? 1 : 2);
	};
	const helds = [
		[
			{ series: id(100, 100, 'call'), qty: -1 },
			{ series: id(150, 100, 'call'), qty: 1 }
		],
		[
			{ series: id(100, 95, 'put'), qty: 2 },
			{ series: id(100, 97.5, 'put'), qty: -2 },
			{ series: id(100, 102.5, 'call'), qty: -2 },
			{ series: id(100, 105, 'call'), qty: 2 }
		],
		[{ series: id(100, 92.5, 'call'), qty: 1.5 }],
		[],
		[{ series: 9999, qty: 1 }]
	];
	for (const held of helds) {
		for (const [to, recenter, f] of [
			[150, false, 100],
			[150, true, 103.7],
			[100, false, 100],
			[200, true, 96.1]
		]) {
			add('rollOrders', [games[0], held, to, recenter, f], m.rollOrders(games[0], held, to, recenter, f));
		}
	}
	add('rollOrders', [games[1], helds[1], 150, true, 101], m.rollOrders(games[1], helds[1], 150, true, 101));
}

// ---- combos.ts (wave 1) ----

const WAVE1_LOG = [
	{ k: 'adv', n: 1 },
	{ k: 'opt', series: 3, side: 'sell', type: 'limit', qty: '1.0', premium: '2.10', tif: 'ioc', combo: 'c1', strategy: 'calendar_call' },
	{ k: 'opt', series: 21, side: 'buy', type: 'limit', qty: '1.0', premium: '4.25', tif: 'ioc', combo: 'c1', strategy: 'calendar_call' },
	{ k: 'adv', n: 5 },
	{ k: 'opt', series: 21, side: 'buy', type: 'market', qty: '1.0', premium: null, combo: 'c1', strategy: 'calendar_call', roll: '1' },
	{ k: 'opt', series: 3, side: 'buy', type: 'market', qty: '1.0', premium: null, combo: 'c1', strategy: 'calendar_call', roll: '1' },
	{ k: 'opt', series: 23, side: 'sell', type: 'market', qty: '1.0', premium: null, combo: 'c1', strategy: 'calendar_call', roll: '1' },
	{ k: 'adv', n: 2 },
	{ k: 'order', type: 'market', side: 'sell', qty: '0.4', combo: 'c1', strategy: 'calendar_call', adjust: '1' },
	{ k: 'adv', n: 1 },
	{ k: 'opt', series: 23, side: 'buy', type: 'market', qty: '1.0', premium: null, combo: 'c1', strategy: 'calendar_call', adjust: '2' },
	{ k: 'cancel', id: 9, combo: 'c1', strategy: 'calendar_call', adjust: '2' },
	{ k: 'opt', series: 5, side: 'buy', type: 'limit', qty: '1.0', premium: null, combo: 'c2', strategy: 'custom' }
];

const ev = (seq, step, type, orderId, extra = {}) => ({ seq, step, type, orderId, ...extra });

const WAVE1_EVENTS = [
	ev(1, 61, 'order', 1, { status: 'accepted', side: 'sell', qty: '1.00000000' }),
	ev(2, 61, 'order', 2, { status: 'accepted', side: 'buy', qty: '1.00000000' }),
	ev(3, 62, 'fill', 1, { price: '2.10000000' }),
	ev(4, 62, 'fill', 2, { price: '4.25000000' }),
	ev(5, 66, 'order', 3, { status: 'accepted', side: 'buy', qty: '1.00000000' }),
	ev(6, 66, 'order', 4, { status: 'accepted', side: 'buy', qty: '1.00000000' }),
	ev(7, 66, 'order', 5, { status: 'accepted', side: 'sell', qty: '1.00000000' }),
	ev(8, 67, 'fill', 3, { price: '4.40000000' }),
	ev(9, 67, 'fill', 4, { price: '0.90000000' }),
	ev(10, 68, 'order', 6, { status: 'accepted', side: 'sell', qty: '0.40000000' }),
	ev(11, 69, 'fill', 6, { price: '101.20000000' }),
	ev(12, 69, 'order', 7, { status: 'accepted', side: 'buy', qty: '1.00000000' })
];

export function combosWave1Cases(m, add) {
	const recs = m.combosFromLog(WAVE1_LOG, 60);
	add('combosFromLog', [WAVE1_LOG, 60], recs);
	for (const events of [WAVE1_EVENTS, WAVE1_EVENTS.slice(0, 9), [...WAVE1_EVENTS, ev(13, 67, 'fill', 5, { price: '2.30000000' })]]) {
		for (const rec of recs) {
			const st = m.comboStatus(events, rec);
			add('comboStatus', [events, rec], st);
			add('comboHoldings', [st], m.comboHoldings(st));
			add('workingLegs', [st], m.workingLegs(st));
		}
	}
	for (const rec of recs) {
		for (const kind of ['roll', 'adjust']) add('nextSubmissionNumber', [rec, kind], m.nextSubmissionNumber(rec, kind));
	}
}

// ---- heatmap.ts and portfolio.ts ----

export function heatmapFixture(h, p, strategies, heatmapResult, games) {
	const { list, add } = cases();
	for (const [bars, rows] of [
		[0, 9],
		[1, 9],
		[8, 9],
		[40, 9],
		[40, 3],
		[100, 25],
		[100, 40],
		[7.6, 9],
		[-3, 9]
	]) {
		add('heatmapTimes', [bars, rows], h.heatmapTimes(bars, rows));
	}
	for (const [f, tick] of [
		[100, 0.01],
		[101.37, 0.05],
		[2534.5, 0.5],
		[0.1234, 0.0001]
	]) {
		add('heatmapRange', [f, tick], h.heatmapRange(f, tick));
	}
	const legs = [
		{ series: 4, side: 'buy', qty: '1.0' },
		{ kind: 'perp', side: 'sell', qty: '2.5', entry: '101.25000000' }
	];
	add('heatmapRequest', [legs, 101.37, 0.01, 40, false], h.heatmapRequest(legs, 101.37, 0.01, 40, false));
	add('heatmapRequest', [[], 100, 0.05, 3, true], h.heatmapRequest([], 100, 0.05, 3, true));
	add('heatmapScale', [heatmapResult.heatmap], h.heatmapScale(heatmapResult.heatmap));
	add('heatmapScale', [{ pnl: [['0', '0']] }], h.heatmapScale({ pnl: [['0', '0']] }));
	for (const [v, s] of [
		[0, 10],
		[5, 10],
		[-5, 10],
		[12, 10],
		[-0.01, 10],
		[3.333, 7]
	]) {
		add('cellStyle', [v, s], h.cellStyle(v, s));
	}
	for (const locale of ['pt-BR', 'en']) {
		for (const [t, x] of [
			[0, 0],
			[1, 3],
			[heatmapResult.heatmap.times.length - 1, heatmapResult.heatmap.prices.length - 1]
		]) {
			add('cellParams', [heatmapResult.heatmap, t, x, locale], h.cellParams(heatmapResult.heatmap, t, x, locale));
		}
	}
	portfolioCases(p, add, games);
	void strategies;
	return { helper: 'heatmap.ts', cases: list };
}

function portfolioCases(p, add, games) {
	const leg = (kind, side, qty) => ({ kind, side, qty, series: kind === 'perp' ? 0 : 4 });
	add('portfolioRows', [{ legs: [leg('perp', 'sell', '2.50000000'), leg('call', 'buy', '1.00000000')] }], p.portfolioRows({ legs: [leg('perp', 'sell', '2.50000000'), leg('call', 'buy', '1.00000000')] }));
	const limits = [
		{ kind: 'delta', limitBp: 500, valueBp: 712, exceeded: true },
		{ kind: 'vega', limitBp: 100, valueBp: 40, exceeded: false },
		{ kind: 'theta', limitBp: 20, valueBp: 21, exceeded: true }
	];
	for (const risk of [{ limits }, { limits: [] }, null]) add('exceededLimits', [risk], p.exceededLimits(risk));
	for (const r of [{}, { riskLimits: { deltaBp: 500, vegaBp: 100 } }, { riskLimits: { deltaBp: 0, gammaBp: 30, thetaBp: 20 } }]) {
		add('declaredLimits', [{ missionRules: r }], p.declaredLimits({ missionRules: r }));
	}
	for (const g of games) {
		for (const d of ['3.27000000', '-48.50000000', '0.04000000']) {
			const risk = { deltaUnits: d };
			const hedge = p.hedgeTicket(g, risk);
			add('hedgeTicket', [g, risk], hedge);
			if (hedge) add('deltaAfterHedge', [g, risk, hedge], p.deltaAfterHedge(g, risk, hedge));
		}
		add('hedgeTicket', [g, null], p.hedgeTicket(g, null));
	}
}
