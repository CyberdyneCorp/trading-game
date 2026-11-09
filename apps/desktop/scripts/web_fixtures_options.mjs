// Web parity fixtures of the strategy builder, its combos and the what-if lab (add-academy-and-strategies
// D3, D4): strategies.ts, combos.ts and whatifLab.ts over fixed inputs. Same format as
// web_fixtures_lib.mjs; replayed by tests/viewmodel/game/test_options_parity.cpp.
import { combosWave1Cases, strategiesWave1Cases } from './web_fixtures_wave1.mjs';

function cases() {
	const list = [];
	const add = (fn, args, out) => list.push({ fn, args, out: out === undefined ? null : out });
	return { list, add };
}

const STRIKES = [90, 92.5, 95, 97.5, 100, 102.5, 105, 107.5, 110];

// Four expiries: two listed, one in its settlement window, one no longer listed.
export function chainOf() {
	const chain = [];
	let id = 1;
	for (const [expiryStep, listed, settling] of [
		[100, true, false],
		[150, true, false],
		[80, true, true],
		[200, false, false]
	]) {
		for (const strike of STRIKES) {
			for (const kind of ['call', 'put']) {
				const itm = kind === 'call' ? Math.max(0, 100 - strike) : Math.max(0, strike - 100);
				const bid = strike === 110 && kind === 'call' ? 0 : itm + 0.5 + (id % 7) / 10;
				chain.push({ id: id++, kind, strike: strike.toFixed(8), expiryStep, listed, settling, bid: bid.toFixed(8), ask: (bid + 0.2).toFixed(8) });
			}
		}
	}
	return chain;
}

// A GameState as far as the builder reads it (the engine's tl_state shape).
export function gameOf({ multiplier = '1', optMultiplier = '1', lot = '0.1', optLot = '0.1', posQty = '0', posEntry = '0', positions = [], orders = [] } = {}) {
	return {
		contract: { multiplier, lotSize: lot },
		position: { qty: posQty, entry: posEntry },
		...(orders.length ? { orders } : {}),
		options: { multiplier: optMultiplier, lotSize: optLot, premiumTick: '0.05', chain: chainOf(), positions }
	};
}

// Working orders around a held long of 4.8: which reduce-only sell stops close all of it.
const order = (id, type, price, qty, extra = {}) => ({ id, type, side: 'sell', qty, filled: '0', price, reduceOnly: true, series: 0, ...extra });
const STOPS = [
	order(1, 'stop_market', '97.50000000', '4.80000000'),
	order(2, 'trailing_stop', '98.20000000', '5.00000000'),
	order(3, 'stop_market', '99.00000000', '4.80000000', { filled: '1.00000000' }),
	order(4, 'take_profit', '105.00000000', '4.80000000'),
	order(5, 'stop_market', '99.10000000', '4.80000000', { reduceOnly: false }),
	order(6, 'stop_market', '99.20000000', '4.80000000', { series: 3 }),
	order(7, 'stop_market', '99.30000000', '4.80000000', { side: 'buy' }),
	order(8, 'stop_market', '0', '4.80000000')
];

function stopCases(m, add) {
	for (const g of [
		gameOf({ posQty: '4.8', posEntry: '99.5', orders: STOPS }),
		gameOf({ posQty: '4.8', posEntry: '99.5', orders: STOPS.slice(2) }),
		gameOf({ posQty: '4.8', posEntry: '99.5', orders: [STOPS[0]] }),
		gameOf({ posQty: '-4.8', posEntry: '99.5', orders: STOPS }),
		gameOf({ orders: STOPS })
	]) {
		add('longStop', [g], m.longStop(g));
		add('heldLong', [g], m.heldLong(g));
	}
	for (const args of [
		[10000, 1, 99.89, 1, 0.1],
		[10000, 1, 99.89, 88.4, 0.1],
		[2500, 2.5, 46.49, 44.7, 0.1],
		[10000, 1, 0.37, 1, 1],
		[10000, 1, null, 1, 0.1],
		[10000, 1, 0, 1, 0.1],
		[10000, 0, 99.89, 1, 0.1],
		[0, 1, 99.89, 1, 0.1],
		[10000, 1, 99.89, 0, 0.1],
		[10000, 0.01, 99.89, 1, 0.1]
	]) {
		add('contractsForRisk', args, m.contractsForRisk(...args));
	}
}

function templateCases(m, add, games) {
	for (const id of m.TEMPLATES) {
		for (const f of [100, 101.2, 96.3, 111, 89]) {
			for (const [n, short] of [
				[1, false],
				[2.5, true]
			]) {
				const legs = m.templateLegs(id, STRIKES, f, n, short);
				add('templateLegs', [id, STRIKES, f, n, short], legs);
				if (!legs) continue;
				add('submissionOrder', [legs], m.submissionOrder(legs));
				add('writesOptions', [legs], m.writesOptions(legs));
				add('legsProblem', [legs], m.legsProblem(legs));
				for (const g of games) {
					for (const e of [100, 150]) add('orderLegs', [g, e, legs], m.orderLegs(g, e, legs));
				}
			}
		}
	}
	add('templateLegs', ['bull_call', [], 100, 1, false], m.templateLegs('bull_call', [], 100, 1, false));
}

function freeLegCases(m, add, games) {
	const call = (strike, side = 'buy', qty = 1) => ({ kind: 'call', side, strike, qty });
	const perp = (qty = 1) => ({ kind: 'perp', side: 'buy', strike: null, qty });
	for (const legs of [
		[],
		[call(100), call(105, 'sell'), call(110), call(95), call(90, 'sell')],
		[perp(), perp(2)],
		[call(100, 'buy', 0)],
		[call(100, 'sell'), perp(), call(95)],
		[call(101)]
	]) {
		add('legsProblem', [legs], m.legsProblem(legs));
		add('submissionOrder', [legs], m.submissionOrder(legs));
		add('writesOptions', [legs], m.writesOptions(legs));
		for (const g of games) add('orderLegs', [g, 100, legs], m.orderLegs(g, 100, legs));
	}
}

export function strategiesFixture(m) {
	const { list, add } = cases();
	const chain = chainOf();
	add('expiriesOf', [chain], m.expiriesOf(chain));
	for (const e of [100, 150, 80, 999]) add('strikesOf', [chain, e], m.strikesOf(chain, e));
	for (const id of m.TEMPLATES) {
		for (const short of [false, true]) {
			add('templateWrites', [id, short], m.templateWrites(id, short));
			add('strategyOf', [id, short], m.strategyOf(id, short));
		}
		add('templateShortable', [id], m.templateShortable(id));
	}
	const games = [gameOf(), gameOf({ multiplier: '10', lot: '0.01', optLot: '1' }), gameOf({ posQty: '4.8', posEntry: '99.5' })];
	templateCases(m, add, games);
	freeLegCases(m, add, games);
	for (const g of games) {
		for (const units of [1, 2.55, 48, 0.004]) add('perpContracts', [g, units], m.perpContracts(g, units));
		add('heldLong', [g], m.heldLong(g));
	}
	for (const [g, held] of [
		[games[2], { qty: '4.8' }],
		[games[1], { qty: '0.37' }],
		[games[0], { qty: '48' }]
	]) {
		add('coverContracts', [g, held], m.coverContracts(g, held));
	}
	const pos = gameOf({
		posQty: '-3.5',
		posEntry: '101.25',
		positions: [
			{ series: 9, qty: '2.00000000', avgPremium: '1.25000000' },
			{ series: 12, qty: '-1.50000000', avgPremium: '0.80000000' }
		]
	});
	const tens = gameOf({ posQty: '10.00000000', posEntry: '620.00000000', positions: [{ series: 9, qty: '-20.00000000', avgPremium: '13.46000000' }] });
	for (const g of [pos, games[0], games[2], tens]) add('positionLegs', [g], m.positionLegs(g));
	const orders = [
		{ series: 9, side: 'buy', qty: '1.0' },
		{ series: 0, side: 'buy', qty: '2.5' },
		{ series: 12, side: 'sell', qty: '1.0' }
	];
	add('comboLegs', [orders], m.comboLegs(orders));
	add('whatifLegs', [orders, null], m.whatifLegs(orders, null));
	const held = { qty: '4.80000000', entry: '99.50000000' };
	add('whatifLegs', [orders, held], m.whatifLegs(orders, held));
	const stopped = { ...held, stop: '98.20000000' };
	add('whatifLegs', [orders.filter((o) => o.series !== 0), stopped], m.whatifLegs(orders.filter((o) => o.series !== 0), stopped));
	stopCases(m, add);
	strategiesWave1Cases(m, add, chain, games);
	return { helper: 'strategies.ts', cases: list };
}

// The debrief's strategy comparison (debrief.ts comparisonLines): the player's row first, the best net
// marked (ties too), refused structures without numbers.
const row = (name, netPnl, extra = {}) => ({ name, entryStep: 62, exitStep: 140, netPnl, maxDrawdown: '35.20000000', marginCalls: 0, refused: null, ...extra });

export function comparisonFixture(m) {
	const { list, add } = cases();
	const comparisons = [
		{ basis: 'player', entryStep: 62, qty: '88.4', rows: [row('iron_condor', '121.11000000'), row('short_strangle', '-310.50000000', { marginCalls: 2, exitStep: 97 })] },
		{ basis: 'reference', entryStep: 60, qty: '10', rows: [row('naked_call', '50.00000000'), row('bull_call_spread', '50.00000000')] },
		{ basis: 'reference', entryStep: 60, qty: '10', rows: [row('iron_condor', '0', { refused: 'insufficient_margin', netPnl: '0', exitStep: 60 }), row('short_strangle', '-12.50000000')] },
		{ basis: 'player', entryStep: 70, qty: '1', rows: [] }
	];
	for (const c of comparisons) {
		for (const net of [0, 121.11, -40.25, 50]) add('comparisonLines', [c, net], m.comparisonLines(c, net));
		add('comparisonBasisKey', [c], m.comparisonBasisKey(c));
	}
	// Rows played by their own entry rule (m20, m22) start on other bars than the basis.
	const own = { basis: 'reference', entryStep: 69, qty: '10', rows: [row('all_out_2r', '196.57000000', { entryStep: 72 })] };
	add('comparisonBasisKey', [own], m.comparisonBasisKey(own));
	return { helper: 'debrief.ts', cases: list };
}

const COMBO_LOG = [
	{ k: 'adv', n: 2 },
	{ k: 'plan', side: 'buy' },
	{ k: 'opt', series: 3, side: 'buy', type: 'market', qty: '1.0', premium: null, combo: 'c1', strategy: 'iron_condor' },
	{ k: 'opt', series: 5, side: 'sell', type: 'market', qty: '1.0', premium: null, combo: 'c1', strategy: 'iron_condor' },
	{ k: 'adv', n: 1 },
	{ k: 'order', type: 'market', side: 'buy', qty: '2.5', combo: 'c2', strategy: 'covered_call' },
	{ k: 'opt', series: 7, side: 'sell', type: 'market', qty: '1.0', premium: null, combo: 'c2', strategy: 'covered_call' },
	{ k: 'cancel', id: 4, combo: 'c2', strategy: 'covered_call' },
	{ k: 'adv', n: 3 },
	{ k: 'opt', series: 9, side: 'buy', type: 'market', qty: '2.0', premium: null, combo: 'c3' }
];

const ev = (seq, step, type, orderId, extra = {}) => ({ seq, step, type, orderId, ...extra });

const COMBO_EVENTS = [
	ev(1, 62, 'order', 1, { status: 'accepted', side: 'buy', qty: '1.00000000' }),
	ev(2, 62, 'order', 2, { status: 'accepted', side: 'sell', qty: '1.00000000' }),
	ev(3, 63, 'fill', 1, { price: '2.10000000' }),
	ev(4, 63, 'order', 2, { status: 'cancelled', reason: 'no_bid' }),
	ev(5, 63, 'order', 3, { status: 'accepted', side: 'buy', qty: '2.50000000' }),
	ev(6, 63, 'order', 4, { status: 'accepted', side: 'sell', qty: '1.00000000' }),
	ev(7, 63, 'order', 3, { status: 'cancelled', reason: 'none' }),
	ev(8, 63, 'order', 4, { status: 'cancelled', reason: 'none' }),
	ev(9, 66, 'order', 5, { status: 'accepted', side: 'buy', qty: '2.00000000' }),
	ev(10, 67, 'fill', 5, { price: '1.50000000' })
];

function statusCases(m, add, recs) {
	const statuses = [];
	for (const rec of recs) {
		add('comboOrderIds', [COMBO_EVENTS, rec], m.comboOrderIds(COMBO_EVENTS, rec));
		const st = m.comboStatus(COMBO_EVENTS, rec);
		statuses.push(st);
		add('comboStatus', [COMBO_EVENTS, rec], st);
		for (const mult of [1, 10]) add('netPremium', [st, mult], m.netPremium(st, mult));
		add('openLegs', [st], m.openLegs(st));
		add('comboContracts', [rec.legs], m.comboContracts(rec.legs));
	}
	const early = COMBO_EVENTS.slice(0, 2);
	add('comboStatus', [early, recs[0]], m.comboStatus(early, recs[0]));
	const both = [...COMBO_EVENTS.filter((e) => e.seq !== 4), ev(11, 63, 'fill', 2, { price: '0.90000000' })];
	const filled = m.comboStatus(both, recs[0]);
	add('comboStatus', [both, recs[0]], filled);
	return [filled, statuses[1], statuses[2]];
}

export function combosFixture(m) {
	const { list, add } = cases();
	for (const combos of [[], [{ id: 'c1' }, { id: 'c3' }], [{ id: 'c2' }, { id: 'x' }]]) add('nextComboId', [combos], m.nextComboId(combos));
	for (const orders of [[], [{ series: 3, side: 'buy', qty: '1.0' }], [{ series: 3, side: 'buy', qty: '1.0' }, { series: 4, side: 'sell', qty: '1.0', premium: '1.10' }]]) {
		add('comboSentKey', [orders], m.comboSentKey(orders));
	}
	add('combosFromLog', [COMBO_LOG, 60], m.combosFromLog(COMBO_LOG, 60));
	const statuses = statusCases(m, add, m.combosFromLog(COMBO_LOG, 60));
	const perpOnly = [{ series: 0, side: 'buy', qty: '2.5' }];
	add('comboContracts', [perpOnly], m.comboContracts(perpOnly));
	add('comboContracts', [[]], m.comboContracts([]));
	const trade = (n, series, side, entryStep, net, rBp) => ({ n, series, side, entryStep, net, rBp });
	const trades = [
		trade(1, 3, 'long', 63, '-40.5', -5000),
		trade(2, 5, 'short', 63, '25.25', 3100),
		trade(3, 9, 'long', 67, '12', null),
		trade(4, 0, 'long', 70, '-3', 0)
	];
	for (const ts of [trades.slice(0, 2), trades.slice(2), [trade(1, 3, 'long', 63, '-40', -2000)]]) {
		const net = ts.reduce((s, t) => s + Number(t.net), 0);
		add('comboR', [ts, net], m.comboR(ts, net));
	}
	add('groupTrades', [trades, statuses], m.groupTrades(trades, statuses));
	add('groupTrades', [trades, []], m.groupTrades(trades, []));
	combosWave1Cases(m, add);
	return { helper: 'combos.ts', cases: list };
}

function requestCases(m, add) {
	for (const [f, pct] of [
		[100, 0],
		[100, -30],
		[101.37, 7],
		[2500.5, 30],
		[0.123, -1]
	]) {
		add('shiftedForward', [f, pct], m.shiftedForward(f, pct));
	}
	const legs = [
		{ kind: 'call', side: 'buy', strike: 100, qty: 1, days: 30, ivPct: 60, entry: 4.12 },
		{ kind: 'put', side: 'sell', strike: 92.5, qty: 2.5, days: 12.5, ivPct: 47.5, entry: 1.07 },
		{ kind: 'perp', side: 'buy', strike: 100, qty: 3, days: 0, ivPct: 0, entry: 99.93 }
	];
	const run = [
		{ series: 4, side: 'buy', qty: '1.0' },
		{ kind: 'perp', side: 'sell', qty: '2.5', entry: '101.25000000' }
	];
	for (const shift of [
		{ price: 0, time: 0, iv: 0 },
		{ price: 7, time: 3, iv: -5 },
		{ price: -30, time: 12, iv: 30 }
	]) {
		add('academyRequest', [legs, shift, 100], m.academyRequest(legs, shift, 100));
		add('academyRequest', [legs.slice(0, 1), shift, 87.65], m.academyRequest(legs.slice(0, 1), shift, 87.65));
		add('runRequest', [run, shift, 101.37], m.runRequest(run, shift, 101.37));
	}
	for (const ls of [legs, legs.slice(2), [], [{ ...legs[0], days: -2 }]]) add('academyMaxDays', [ls], m.academyMaxDays(ls));
	// Whole quantities of 10 and more are written as String(n) writes them ("10", never "1e+01").
	const round = [
		{ ...legs[0], qty: 10, strike: 620 },
		{ ...legs[1], qty: 20, strike: 600 },
		{ ...legs[2], qty: 100, entry: 620 }
	];
	add('academyRequest', [round, { price: 0, time: 0, iv: 0 }, 620], m.academyRequest(round, { price: 0, time: 0, iv: 0 }, 620));
}

export function whatifLabFixture(m, condor, call) {
	const { list, add } = cases();
	requestCases(m, add);
	for (const [steps, step] of [
		[[100, 150], 60],
		[[], 60],
		[[50], 60],
		[[150, 100, 120], 99]
	]) {
		add('runMaxBars', [steps, step], m.runMaxBars(steps, step));
	}
	for (const [s, max] of [
		[{ price: 45, time: 9, iv: -31 }, 5],
		[{ price: -2, time: -1, iv: 12 }, 0],
		[{ price: 3, time: 2, iv: 4 }, 10]
	]) {
		add('clampShift', [s, max], m.clampShift(s, max));
	}
	for (const [which, unlocked] of [
		['price', ['price', 'time', 'iv']],
		['iv', ['price']],
		['time', ['time']]
	]) {
		const s = { price: 1, time: 2, iv: 3 };
		add('setSlider', [s, which, 9, unlocked], m.setSlider(s, which, 9, unlocked));
	}
	for (const qty of ['1', '85.4', '47.90000000', '1234.5', '0.12345678', '3']) {
		for (const locale of ['pt-BR', 'en']) {
			for (const l of [
				{ kind: 'call', side: 'buy', qty, strike: '105.00000000' },
				{ kind: 'perp', side: 'sell', qty, strike: null }
			]) {
				add('legLabel', [l, locale, 'perp'], m.legLabel(l, locale, 'perp'));
			}
		}
	}
	for (const r of [condor, call]) {
		add('strikesOf', [r], m.strikesOf(r));
		add('unboundedAbove', [r], m.unboundedAbove(r));
		for (const [w, h] of [
			[320, 140],
			[600, 200]
		]) {
			add('chartGeometry', [r, w, h, m.strikesOf(r)], m.chartGeometry(r, w, h, m.strikesOf(r)));
		}
	}
	const naked = { ...call, expiry: { ...call.expiry, maxLoss: null, breakevens: ['104.12000000'] } };
	add('unboundedAbove', [naked], m.unboundedAbove(naked));
	const flat = { ...call, today: call.today.slice(0, 1) };
	add('chartGeometry', [flat, 320, 140, []], m.chartGeometry(flat, 320, 140, []));
	return { helper: 'whatifLab.ts', cases: list };
}
