// The input grids of the web parity fixtures (gen_web_fixtures.mjs). Each fixture is
// { "helper": "<web file>", "cases": [{ "fn": "<web function>", "args": [...], "out": <result> }] };
// tests/viewmodel/test_web_parity.cpp calls the C++ port of `fn` with `args` and compares with `out`
// (numbers with a 1e-9 relative tolerance, everything else exactly).

import { combosFixture, comparisonFixture, gameOf, strategiesFixture, whatifLabFixture } from './web_fixtures_options.mjs';
import { heatmapFixture, orderRulesFixture } from './web_fixtures_wave1.mjs';

const SIDES = ['buy', 'sell'];
const LOCALES = ['pt-BR', 'en'];

const PLAIN = { multiplier: 1, takerFee: 0.0005, maintenanceMargin: 0.005, halfSpread: 0.0002, slippage: 0.0003, tiers: [] };
const TIERED = {
	multiplier: 1,
	takerFee: 0.0006,
	maintenanceMargin: 0.005,
	halfSpread: 0.0002,
	slippage: 0.0001,
	tiers: [
		{ floor: 0, mmr: 0.005, maxLeverage: 20 },
		{ floor: 50000, mmr: 0.01, maxLeverage: 10 },
		{ floor: 250000, mmr: 0.025, maxLeverage: 4 }
	]
};
const CONTRACT10 = { ...PLAIN, multiplier: 10 };
const COSTS = [PLAIN, TIERED, CONTRACT10];

function cases() {
	const list = [];
	const add = (fn, args, out) => list.push({ fn, args, out: out === undefined ? null : out });
	return { list, add };
}

function sizingFixture(m) {
	const { list, add } = cases();
	for (const k of COSTS) {
		for (const side of SIDES) {
			for (const mark of [100, 2500.5]) add('estimatedMarketEntry', [side, mark, k], m.estimatedMarketEntry(side, mark, k));
			for (const qty of [0, 1, 16.6, 600]) {
				for (const margin of [10, 500, 20000]) add('liquidationPrice', [side, qty, 95.56, margin, k], m.liquidationPrice(side, qty, 95.56, margin, k));
			}
			for (const lev of [1, 3, 20]) add('releveredLiquidation', [side, -40, 101.5, lev, k], m.releveredLiquidation(side, -40, 101.5, lev, k));
		}
		for (const notional of [0, 49999, 50000, 300000]) add('marginTier', [k, notional], m.marginTier(k, notional));
		const inputs = [
			{ side: 'buy', qty: 16.6, entry: 100, leverage: 1, stop: 95 },
			{ side: 'buy', qty: 600, entry: 100, leverage: 20, stop: 90 },
			{ side: 'sell', qty: 5, entry: 99.5, leverage: 3, stop: null },
			{ side: 'sell', qty: 2000, entry: 150, leverage: 10, stop: 151 }
		];
		const positions = [undefined, { qty: 10, entry: 98, margin: 980, leverage: 1 }, { qty: -12, entry: 101, margin: 404, leverage: 3 }];
		for (const input of inputs) {
			for (const pos of positions) {
				add('estimate', pos ? [input, k, pos] : [input, k], m.estimate(input, k, pos));
				if (pos) add('liquidationAfter', [input, pos, k], m.liquidationAfter(input, pos, k));
			}
		}
		for (const [eq, pct, entry, stop] of [[10000, 1, 100, 95], [10000, 2.5, 100, 104], [523.4, 0.5, 2500, 2450], [10000, 1, 100, 100]]) {
			add('qtyForRisk', [eq, pct, entry, stop, k, 0.1], m.qtyForRisk(eq, pct, entry, stop, k, 0.1));
		}
		for (const lev of [1, 5, 20]) add('requiredMargin', [12.3, 99.9, lev, k], m.requiredMargin(12.3, 99.9, lev, k));
		for (const [qty, available, from, cap] of [[10, 5000, 1, 10], [100, 5000, 1, 10], [3000, 10000, 2, 20], [50, 0, 1, 5]]) {
			add('fitToMargin', [qty, 100, available, from, cap, k, 0.1], m.fitToMargin(qty, 100, available, from, cap, k, 0.1));
		}
		for (const notional of [0, 999.99, 1000, 123456]) add('qtyForNotional', [notional, 99.7, k, 0.1], m.qtyForNotional(notional, 99.7, k, 0.1));
		for (const pct of [10, 100, 250]) add('qtyForEquityPct', [8000, pct, 99.7, k, 0.01], m.qtyForEquityPct(8000, pct, 99.7, k, 0.01));
		const x = { equity: 10000, entry: 100, stop: 95, qty: 7.77, amount: 2 };
		for (const by of ['qty', 'notional', 'equity', 'risk']) {
			add('sizedQty', [by, x, k, 0.1], m.sizedQty(by, x, k, 0.1));
			add('sizedQty', [by, { ...x, stop: null, amount: 5000 }, k, 0.1], m.sizedQty(by, { ...x, stop: null, amount: 5000 }, k, 0.1));
		}
	}
	for (const [position, side, qty] of [[0, 'buy', 1], [10, 'buy', 5], [10, 'sell', 4], [10, 'sell', 10], [10, 'sell', 16.6], [-3.3, 'buy', 3.3], [-3.3, 'buy', 1]]) {
		add('opposingEffect', [position, side, qty, 0.1], m.opposingEffect(position, side, qty, 0.1));
	}
	return { helper: 'sizing.ts', cases: list };
}

function stopsFixture(m) {
	const { list, add } = cases();
	const bars = [
		[100, 101, 99, 100.5, 10],
		[100.5, 102.3, 100.1, 102, 12],
		[102, 102.5, 98.4, 99, 30],
		[99, 100, 97.9, 99.8, 22],
		[99.8, 103.1, 99.5, 103, 18]
	];
	for (const side of SIDES) {
		for (const atr of [null, 0, 1.25]) {
			for (const mult of [1, 2.5, 100]) add('stopFromAtr', [side, 100, atr, mult], m.stopFromAtr(side, 100, atr, mult));
		}
		for (const pct of [0, 1, 2.5, 150]) add('stopFromPct', [side, 95.56, pct], m.stopFromPct(side, 95.56, pct));
		for (const pct of [0.5, 1, 7.25, 99.99]) add('trailTriggerFromPercent', [side, 101.37, pct, 0.01], m.trailTriggerFromPercent(side, 101.37, pct, 0.01));
		for (const entry of [97, 101, 105]) {
			add('swingStop', [side, entry, bars, 0.01], m.swingStop(side, entry, bars, 0.01));
			add('swingStop', [side, entry, bars, 0.01, 2], m.swingStop(side, entry, bars, 0.01, 2));
		}
		for (const stop of [null, 95, 100, 104]) {
			for (const r of [1, 2]) add('targetFromR', [side, 100, stop, r], m.targetFromR(side, 100, stop, r));
			for (const tp of [null, 90, 110]) add('rewardRisk', [side, 100, stop, tp], m.rewardRisk(side, 100, stop, tp));
		}
		for (const level of [95, 100, 105]) {
			add('stopOnLosingSide', [side, 100, level], m.stopOnLosingSide(side, 100, level));
			add('targetOnWinningSide', [side, 100, level], m.targetOnWinningSide(side, 100, level));
		}
	}
	for (const pct of [-1, 0, 0.00004, 0.5, 1.2345678, 99.9, 100, Infinity]) add('trailRateFromPercent', [pct], m.trailRateFromPercent(pct));
	for (const [level, atr] of [[null, 1], [95, null], [95, 0], [104.5, 2], [0, 2]]) add('distanceOf', [100, level, atr], m.distanceOf(100, level, atr));
	return { helper: 'stops.ts', cases: list };
}

function optionsFixture(m) {
	const { list, add } = cases();
	const quotes = [
		{ id: 1, kind: 'call', strike: '100', expiryStep: 96, bid: '2.1', ask: '2.3', delta: '0.52', gamma: '0.04', theta: '-0.02', vega: '0.11' },
		{ id: 2, kind: 'put', strike: '97.5', expiryStep: 85, bid: '1.05', ask: '1.2', delta: '-0.31', gamma: '0.03', theta: '-0.015', vega: '0.09' },
		{ id: 3, kind: 'call', strike: '1250', expiryStep: 120, bid: '40.5', ask: '42', delta: '0.4', gamma: '0.002', theta: '-0.5', vega: '1.7' }
	];
	for (const q of quotes) {
		for (const side of SIDES) {
			const leg = m.legFromQuote(q, side, 3, 10);
			add('legFromQuote', [q, side, 3, 10], leg);
			for (const s of [0, 90, 97.5, 100, 103.3, 1300]) add('payoffAt', [leg, s], m.payoffAt(leg, s));
			add('breakeven', [leg], m.breakeven(leg));
			add('maxLoss', [leg], m.maxLoss(leg));
			for (const f of [100, 1250]) {
				add('optionFee', [leg, f, 0.0003], m.optionFee(leg, f, 0.0003));
				add('optionBuyCost', [leg, f, 0.0003], m.optionBuyCost(leg, f, 0.0003));
			}
			add('payoffCurve', [leg, 80, 120, 8], m.payoffCurve(leg, 80, 120, 8));
		}
		for (const f of [90, 100, 110, 1250]) add('writeMargin', [q, f, 2, 10], m.writeMargin(q, f, 2, 10));
		for (const locale of LOCALES) add('seriesLabel', [q, locale], m.seriesLabel(q, locale));
	}
	const book = { multiplier: '10', chain: quotes, positions: [{ series: 1, qty: '2' }, { series: 2, qty: '-1.5' }, { series: 9, qty: '4' }] };
	add('positionGreeks', [book], m.positionGreeks(book));
	return { helper: 'optionsMath.ts', cases: list };
}

function formatFixture(m) {
	const { list, add } = cases();
	// No '': the engine never sends an empty decimal, and the desktop reads "" as absent (the web's null),
	// while JavaScript's Number('') is 0.
	const decimals = ['0', '-0.001', '0.005', '1234.5', '-1234.567', '98765.4321', '1000000.555', '0.00012', 'abc'];
	const numbers = [0, -0.004, 0.005, 1.005, 2.675, 1234.5, -1234.567, 1e6 + 0.555, 12.3456789, -0.5, Infinity, NaN];
	for (const locale of LOCALES) {
		for (const d of decimals) {
			add('fmtPrice', [d, locale], m.fmtPrice(d, locale));
			add('fmtQty', [d, locale], m.fmtQty(d, locale));
			add('fmtMoney', [d, locale, false], m.fmtMoney(d, locale));
		}
		for (const v of numbers) {
			for (const signed of [false, true]) {
				add('fmtMoney', [v, locale, signed], m.fmtMoney(v, locale, signed));
				for (const digits of [0, 1, 2, 4]) add('fmtNum', [v, locale, digits, signed], m.fmtNum(v, locale, digits, signed));
				add('fmtPct', [v / 100, locale, 2, signed], m.fmtPct(v / 100, locale, 2, signed));
			}
		}
		for (const s of ['1.000', '1,000', '12.345,6', '1,234.5', '1.5', '1,5', '10.00', '1.0000', '-2.500', ' 3 000 ']) add('parseAmount', [s, locale], m.parseAmount(s, locale));
		for (const [price, tick] of [[99.955, 0.01], [100, 0.5], [12345.678, 0.1], [0, 0.01]]) add('priceInputText', [price, tick, locale], m.priceInputText(price, tick, locale));
		for (const [qty, lot] of [[16.66, 0.1], [0.05, 0.1], [3, 1], [1.23456, 0.001]]) add('qtyInputText', [qty, lot, locale], m.qtyInputText(qty, lot, locale));
	}
	for (const s of ['1,5', '1.5', '1.234,5', '1,234.5', ' 12 ', '', 'abc', '1e3', '-0,25', '+.5', '5.', '1.2.3', '1,2,3']) add('parseDecimal', [s], m.parseDecimal(s));
	for (const v of [-2, -0, 0, 3]) add('signMark', [v], m.signMark(v));
	for (const [qty, lot] of [[16.66, 0.1], [0.3, 0.1], [0.05, 0.1], [-1, 0.1], [7, 1], [2.9999999999, 1], [1.23456, 0.001], [NaN, 0.1]]) add('qtyToLot', [qty, lot], m.qtyToLot(qty, lot));
	for (const [price, tick] of [[99.955, 0.01], [99.954, 0.01], [100.25, 0.5], [0, 0.01], [-3, 0.01], [12345.678, 1]]) add('priceToTick', [price, tick], m.priceToTick(price, tick));
	return { helper: 'format.ts', cases: list };
}

// The web passes the focused DOM element; the desktop passes the kind of the focused Raven widget.
const FOCUS = {
	none: null,
	text: { tagName: 'INPUT', type: 'text' },
	textarea: { tagName: 'TEXTAREA' },
	select: { tagName: 'SELECT' },
	slider: { tagName: 'INPUT', type: 'range' },
	radio: { tagName: 'INPUT', type: 'radio' },
	checkbox: { tagName: 'INPUT', type: 'checkbox' },
	button: { tagName: 'BUTTON' }
};
const KEYS = [' ', 'b', 'S', '?', 'Enter', 'Escape', 'ArrowUp', 'ArrowLeft', 'Home', 'PageDown', 'Tab'];

function hotkeysFixture(m) {
	const { list, add } = cases();
	for (const [focus, el] of Object.entries(FOCUS)) {
		for (const key of KEYS) add('keepsKey', [focus, key], m.keepsKey(el, key));
	}
	return { helper: 'hotkeys.ts', cases: list };
}

async function advisorFixture(m, i18n, load) {
	const { list, add } = cases();
	for (const vote of [-3, -2, -1, 0, 1, 2, 5]) add('voteMark', [vote], m.voteMark(vote));
	for (const bp of [0, 49, 50, 149, 150, 4550, 10000]) add('pctFromBp', [bp], m.pctFromBp(bp));
	const patterns = [
		{ step: 40, id: 'hammer', bars: 1, bias: 1 },
		{ step: 40, id: 'shooting_star', bars: 1, bias: -1 },
		{ step: 44, id: 'bearish_engulfing', bars: 2, bias: -1 }
	];
	const { WEB_REASON_CODES } = await reasonCodes(load);
	for (const locale of LOCALES) {
		i18n.i18n.locale = locale;
		for (const code of WEB_REASON_CODES) {
			for (const [vote, ref] of [[1, 40], [-1, 40], [-2, 44], [0, 12]]) {
				const r = { code, vote, ref };
				add('reasonLine', [r, patterns, locale], m.reasonLine(r, patterns, locale));
			}
		}
	}
	i18n.i18n.locale = 'pt-BR';
	return { helper: 'advisor.ts', cases: list };
}

// The reason codes the web catalog has a line for (adv.r.<code>), so the grid follows the web.
async function reasonCodes(load) {
	const mod = await load('i18n.svelte.ts');
	const t = mod.t;
	const known = ['band_fade', 'event_ahead', 'funding_crowded', 'low_vol_squeeze', 'macd_momentum', 'news_early', 'news_priced', 'pattern', 'regime_range', 'regime_trend_down', 'regime_trend_up', 'rsi_extreme'];
	return { WEB_REASON_CODES: known.filter((c) => t(`adv.r.${c}`) !== `adv.r.${c}`) };
}

function pointsFixture(m) {
	const { list, add } = cases();
	for (const points of [-2, 0, 3, 9.7, 10, 14]) add('allowanceFor', [{ advisorPoints: points, settledRuns: [] }], m.allowanceFor({ advisorPoints: points, settledRuns: [] }));
	// A sequence of settlements on one balance: repeats are ignored, the balance stays within 0..10.
	let p = { advisorPoints: 3, settledRuns: [] };
	for (const [runId, used, earned] of [['r1', 2, 1], ['r1', 2, 1], ['', 1, 0], ['r2', 0, 9], ['r3', 5, 0], ['r4', 9, 0], ['r5', 0, 2]]) {
		const before = p;
		p = m.settleRun(p, runId, used, earned);
		add('settleRun', [before, runId, used, earned], p);
	}
	return { helper: 'points.ts', cases: list };
}

// What the C++ test compares of a campaign progress after each step.
function campaignSummary(p) {
	const run = (c) => c && { id: c.id, capital: c.capital, index: c.index, status: c.status, legs: c.legs.map((l) => ({ runId: l.runId, missionId: l.missionId, startCapital: l.startCapital, endCapital: l.endCapital, result: l.result })) };
	return { campaign: run(p.campaign), history: p.campaignHistory.map(run), assessed: p.assessed };
}

function campaignFixture(m, curriculum) {
	const { list, add } = cases();
	const missions = curriculum.campaignMissions();
	const steps = [
		['start', 'c1', 1000],
		['settle', { id: 'c1', index: 0 }, { runId: 'a', missionId: missions[0], result: 'finished', equity: '10450.5' }],
		['settle', { id: 'c1', index: 0 }, { runId: 'a', missionId: missions[0], result: 'finished', equity: '10450.5' }],
		['settle', { id: 'c1', index: 1 }, { runId: 'b', missionId: missions[1], result: 'left', equity: '9800' }],
		['settle', { id: 'c1', index: 1 }, { runId: 'c', missionId: missions[0], result: 'finished', equity: '9800' }],
		['settle', { id: 'c1', index: 1 }, { runId: 'd', missionId: missions[1], result: 'finished', equity: '99.99' }],
		['start', 'c2', 2000],
		['abandon'],
		['start', 'c3', 3000]
	];
	let p = { campaign: null, campaignHistory: [], assessed: [] };
	for (const [i, mission] of missions.entries()) {
		steps.push(['settle', { id: 'c3', index: i }, { runId: `z${i}`, missionId: mission, result: 'finished', equity: String(10000 + 100 * i) }]);
	}
	for (const step of steps) {
		const [op, a, b] = step;
		if (op === 'start') p = m.startCampaign(p, a, b);
		else if (op === 'abandon') p = m.abandonCampaign(p);
		else p = m.settleCampaignRun(p, a, b);
		add('step', step, { ...campaignSummary(p), next: m.nextCampaignMission(p.campaign) });
	}
	for (const equity of ['0', '99.99', '100', '100.0001', 'abc']) add('belowCampaignMinimum', [equity], m.belowCampaignMinimum(equity));
	const history = [
		{ id: 'h1', status: 'complete', capital: '12000.5' },
		{ id: 'h2', status: 'abandoned', capital: '50000' },
		{ id: 'h3', status: 'complete', capital: '13000' },
		{ id: 'h4', status: 'complete', capital: '9000' }
	];
	add('bestCompletedCapital', [history], m.bestCompletedCapital(history));
	add('bestCompletedCapital', [history.slice(1, 2)], m.bestCompletedCapital(history.slice(1, 2)));
	// Progress v4: a run stored before v4 has no order snapshot and keeps the 17-mission order; a new run
	// snapshots today's order (design D14 "Campaign order snapshot").
	const legacy = (index) => ({ id: 'old', startedAt: 1, capital: '10000', index, status: 'active', legs: [] });
	for (const [index, missionId] of [[5, 'm04-options-buyer'], [5, 'm22-scale-manage'], [16, 'm12-capstone'], [20, 'm12-capstone']]) {
		const run = legacy(index);
		const after = m.settleCampaignRun({ campaign: run, campaignHistory: [], assessed: [] }, { id: 'old', index }, { runId: 'l', missionId, result: 'finished', equity: '10500' });
		add('legacy', [run, { id: 'old', index }, { runId: 'l', missionId, result: 'finished', equity: '10500' }], { ...campaignSummary(after), next: m.nextCampaignMission(after.campaign), orderBefore: m.campaignOrder(run) });
	}
	add('newCampaignOrder', [], m.newCampaign('n', 1).order);
	// Elective stages (design D14): a finished, non-assisted run outside the campaign assesses its mission.
	const elective = (id) => curriculum.isElectiveMission(id);
	const outs = [
		{ missionId: 'm18-gamma-scalp', finished: true, assisted: false, campaign: false },
		{ missionId: 'm21-calendar', finished: false, assisted: false, campaign: false },
		{ missionId: 'm19-funding-carry', finished: true, assisted: true, campaign: false },
		{ missionId: 'm20-event-vol', finished: true, assisted: false, campaign: true },
		{ missionId: 'm22-scale-manage', finished: true, assisted: false, campaign: false },
		{ missionId: 'm18-gamma-scalp', finished: true, assisted: false, campaign: false }
	];
	for (const assessed of [[], ['m18-gamma-scalp']]) {
		for (const out of outs) add('assessElective', [assessed, out], m.assessElective({ assessed, campaign: null, campaignHistory: [] }, out, elective).assessed);
	}
	return { helper: 'campaign.ts', cases: list };
}

function curriculumFixture(m) {
	const { list, add } = cases();
	const missions = m.campaignMissions();
	add('campaignMissions', [], missions);
	for (const id of [...new Set(m.STAGES.flatMap((s) => s.missions))]) add('isElectiveMission', [id], m.isElectiveMission(id));
	const assessedSets = [[], missions.slice(0, 1), missions.slice(0, 3), missions.slice(0, 7), missions];
	const lessonSets = [[], ['foundations-bar']];
	for (const assessed of assessedSets) {
		for (const lessons of lessonSets) {
			for (const s of m.STAGES) {
				add('stagePassed', [s.id, assessed, lessons], m.stagePassed(s.id, assessed, lessons));
				add('stageStatus', [s.id, assessed, lessons], m.stageStatus(s.id, assessed, lessons));
			}
			add('curriculumSummary', [assessed, lessons], m.curriculumSummary(assessed, lessons));
		}
		for (const mission of missions) add('campaignPermissions', [mission, assessed], m.campaignPermissions(mission, assessed));
	}
	for (const mission of [...missions, 'm99-unknown']) add('stagesOf', [mission], m.stagesOf(mission));
	const done = [[], ['foundations-bar', 'risk-sizing'], m.STAGES.flatMap((s) => s.lessons)];
	for (const lessons of done) {
		for (const s of m.STAGES) {
			add('lessonProgress', [s.id, lessons], m.lessonProgress(s.id, lessons));
			add('recommendedLesson', [s.id, lessons], m.recommendedLesson(s.id, lessons));
		}
	}
	for (const code of ['options', 'risk', 'futures1', 'strategies', 'nope', '']) add('lessonCodeStage', [code], m.lessonCodeStage(code));
	for (const u of ['short', 'leverage', 'optionsBuy', 'optionsWrite']) add('unlockStage', [u], m.unlockStage(u));
	return { helper: 'curriculum.ts', cases: list };
}

// A terminal state reduced to what audioRules reads; the C++ test builds its GameState from the same.
function audioState(s) {
	return {
		status: s.status ?? 'active',
		step: s.step ?? 50,
		account: { mark: String(s.mark ?? 100), liquidationPrice: s.liq == null ? null : String(s.liq), equity: String(s.equity ?? 1000) },
		position: { qty: String(s.qty ?? 0) },
		options: s.mm == null ? undefined : { maintenanceMargin: String(s.mm) },
		events: s.events ?? []
	};
}

function audioFixture(m) {
	const { list, add } = cases();
	const states = [
		{},
		{ qty: 1, liq: 90 },
		{ qty: 1, liq: 95.1 },
		{ qty: 1, liq: 94.9 },
		{ qty: -2, liq: 106 },
		{ qty: -2, liq: 104.9 },
		{ qty: 1, liq: 92.5 },
		{ qty: 0, liq: 99 },
		{ qty: 1, liq: null },
		{ mm: 300, equity: 700 },
		{ mm: 300, equity: 500 },
		{ mm: 300, equity: 900 },
		{ events: [{ seq: 1, step: 48, type: 'margin_call' }] },
		{ events: [{ seq: 1, step: 47, type: 'liquidation' }] },
		{ status: 'finished', qty: 1, liq: 99.9 }
	];
	for (const s of states) {
		const g = audioState(s);
		for (const wasTense of [false, true]) add('inDanger', [s, wasTense], m.inDanger(g, wasTense));
		for (const screen of ['terminal', 'menu', 'debrief']) {
			for (const previous of [null, 'game', 'tense']) add('moodFor', [screen, s, previous], m.moodFor(screen, g, previous));
		}
	}
	add('moodFor', ['terminal', null, null], m.moodFor('terminal', null, null));
	const ev = (seq, type, pnl) => ({ seq, step: seq, type, pnl: pnl == null ? undefined : String(pnl) });
	const streams = [
		[],
		[ev(1, 'order_accepted'), ev(2, 'fill', 0)],
		[ev(1, 'fill', 0), ev(2, 'fill', 12.5)],
		[ev(1, 'fill', -3), ev(2, 'fill', 2)],
		[ev(1, 'fill', 20), ev(2, 'liquidation')],
		[ev(1, 'option_settlement', 4), ev(2, 'funding', 9)],
		[ev(3, 'fill', -1), ev(4, 'option_settlement', 1)]
	];
	for (const events of streams) {
		for (const after of [0, 1, 2]) add('stingFor', [events, after], m.stingFor(events, after));
	}
	// StingTracker: the first state of a run (new or resumed) is a baseline, later events play once.
	const tracker = new m.StingTracker();
	const observed = [
		['run-1', [ev(1, 'fill', 50)]],
		['run-1', [ev(1, 'fill', 50), ev(2, 'fill', -10)]],
		['run-1', [ev(1, 'fill', 50), ev(2, 'fill', -10)]],
		['run-2', [ev(1, 'fill', -10), ev(2, 'liquidation')]],
		['run-2', [ev(1, 'fill', -10), ev(2, 'liquidation'), ev(3, 'fill', 7)]]
	];
	for (const [run, events] of observed) add('StingTracker.observe', [run, events], tracker.observe(run, events));
	return { helper: 'audioRules.ts', cases: list };
}

// ---- Academy (add-academy-and-strategies): lesson widgets, typed answers, review, glossary ----

const leg = (kind, side, qty, strike, price) => (kind === 'perp' ? { kind, side, qty, price } : { kind, side, qty, strike, price });
const LEG_SETS = [
	[leg('call', 'buy', 1, 100, 4)],
	[leg('put', 'buy', 1, 100, 4)],
	[leg('call', 'buy', 1, 100, 4), leg('put', 'buy', 1, 100, 4)],
	[leg('call', 'buy', 1, 100, 5.5), leg('call', 'sell', 1, 110, 1.5)],
	[leg('put', 'buy', 1, 90, 0.8), leg('put', 'sell', 1, 95, 2.3), leg('call', 'sell', 1, 105, 2.3), leg('call', 'buy', 1, 110, 0.8)],
	[leg('put', 'sell', 1, 95, 2), leg('call', 'sell', 1, 105, 2)],
	[leg('perp', 'buy', 1, 0, 100), leg('call', 'sell', 1, 105, 2)],
	[leg('perp', 'buy', 1, 0, 100), leg('put', 'buy', 1, 95, 2)],
	[leg('perp', 'sell', 2, 0, 2400), leg('call', 'buy', 1, 2400, 75)],
	[leg('call', 'sell', 3, 100, 1)],
	[leg('put', 'sell', 1, 100, 100)],
	[leg('call', 'buy', 0.5, 50, 2.25), leg('call', 'sell', 1, 55, 1), leg('call', 'buy', 0.5, 60, 0.4)]
];

function payoffFixture(m) {
	const { list, add } = cases();
	for (const legs of LEG_SETS) {
		for (const s of [0, 50, 90, 95, 100, 104.5, 110, 2400, 3000]) add('pnlAt', [legs, s], m.pnlAt(legs, s));
		add('legPnlAt', [legs[0], 97.5], m.legPnlAt(legs[0], 97.5));
		add('breakevens', [legs], m.breakevens(legs));
		add('maxProfit', [legs], m.maxProfit(legs));
		add('maxLoss', [legs], m.maxLoss(legs));
		add('payoffCurve', [legs, 80, 120, 8], m.payoffCurve(legs, 80, 120, 8));
	}
	return { helper: 'content/payoff.ts', cases: list };
}

const ticket = (o) => ({ kind: 'ticket', market: 'X', equity: 10000, entry: 100, side: 'buy', qty: 25, leverage: 5, maxLeverage: 20, stop: 96, target: 108, mmr: 0.01, lot: 1, edit: ['qty'], ...o });

function ticketSandboxFixture(m) {
	const { list, add } = cases();
	const widgets = [ticket({}), ticket({ side: 'sell', entry: 250, stop: 260, target: 230, qty: 10 }), ticket({ stop: null, target: null, leverage: 20 }), ticket({ equity: 2000, entry: 50, qty: 3, leverage: 10, stop: 44, target: null })];
	for (const w of widgets) {
		const base = m.initialInputs(w);
		add('initialInputs', [w], base);
		const variants = [base, { ...base, qty: 0 }, { ...base, qty: -3 }, { ...base, leverage: 40 }, { ...base, leverage: 0 }, { ...base, side: base.side === 'buy' ? 'sell' : 'buy' }, { ...base, stop: null }, { ...base, target: 120 }];
		for (const x of variants) {
			const out = m.sandboxOutputs(w, x);
			add('sandboxOutputs', [w, x], out);
			for (const f of ['side', 'qty', 'leverage', 'stop', 'target', 'notional', 'margin', 'liquidation', 'riskAtStop', 'riskPct', 'rTarget']) {
				add('sandboxValue', [f, x, out], m.sandboxValue(f, x, out));
			}
		}
	}
	for (const [v, t, tol] of [[100, 100, 0], [104, 100, 4], [104.0000001, 100, 4], [null, 1, 1], [Infinity, 1, 1], [96.1, 100, 4]]) add('reachesTarget', [v, t, tol], m.reachesTarget(v, t, tol));
	return { helper: 'content/ticketSandbox.ts', cases: list };
}

function lessonRunFixture(m) {
	const { list, add } = cases();
	const inputs = ['6', '−1,20', '-1.2', '2.340', '2,340', '6,25%', '6.25 %', '2 R', '2R ', '5 cr', '5CR', '7×', '1.234,5', '1,234.5', 'abc', '', ' 18 ', '–30', '0,2R'];
	for (const input of inputs) for (const L of LOCALES) add('readTypedNumber', [input, L], m.readTypedNumber(input, L));
	return { helper: 'lessonRun.svelte.ts', cases: list };
}

function reviewFixture(m) {
	const { list, add } = cases();
	for (const [d, n] of [['2026-01-31', 1], ['2026-02-27', 3], ['2024-02-28', 1], ['2026-12-25', 7], ['2026-03-08', 21], ['2026-10-31', 1]]) add('addDays', [d, n], m.addDays(d, n));
	const today = '2026-03-10';
	let r = m.scheduleOnComplete({}, 'a', today);
	add('scheduleOnComplete', [{}, 'a', today], r);
	add('scheduleOnComplete', [r, 'a', '2026-03-12'], m.scheduleOnComplete(r, 'a', '2026-03-12'));
	let day = today;
	for (const right of [true, true, false, true, true, true, true, true]) {
		const next = m.applyReview(r, 'a', right, day);
		add('applyReview', [r, 'a', right, day], next);
		r = next;
		day = m.addDays(day, 2);
	}
	add('applyReview', [{}, 'b', true, today], m.applyReview({}, 'b', true, today));
	const sched = { a: { box: 1, due: '2026-03-10' }, b: { box: 2, due: '2026-03-11' }, c: { box: 5, due: '2026-01-01' }, d: { box: 3, due: '2026-02-01' } };
	for (const d of ['2026-03-09', '2026-03-10', '2026-03-11']) add('dueReviews', [sched, d, ['d', 'c', 'b', 'a', 'z']], m.dueReviews(sched, d, ['d', 'c', 'b', 'a', 'z']));
	add('isRetired', [sched.c], m.isRetired(sched.c));
	add('isRetired', [sched.a], m.isRetired(sched.a));
	add('readReviews', [{ a: { box: 2, due: '2026-01-02' }, b: { box: 0, due: '2026-01-02' }, c: { box: 9, due: '2026-01-02' }, d: { box: 1.5, due: '2026-01-02' }, e: { box: 1, due: '2026/01/02' }, f: 'x' }], m.readReviews({ a: { box: 2, due: '2026-01-02' }, b: { box: 0, due: '2026-01-02' }, c: { box: 9, due: '2026-01-02' }, d: { box: 1.5, due: '2026-01-02' }, e: { box: 1, due: '2026/01/02' }, f: 'x' }));
	return { helper: 'review.ts', cases: list };
}

function glossaryFixture(m) {
	const { list, add } = cases();
	for (const q of ['', 'liquidacao', 'LIQUIDAÇÃO', 'margem', 'margin', 'call', 'theta', 'stop', '  risco ', 'zzz', 'é', 'spread']) {
		for (const L of LOCALES) add('searchTerms', [q, L], m.searchTerms(q, L).map((t) => t.id));
	}
	for (const s of ['Liquidação', ' Ação ', 'Ç', 'naïve', 'ÀÉÎÕÜ']) add('fold', [s], m.fold(s));
	return { helper: 'content/glossary.ts', cases: list };
}

// Journal statistics (add-run-sharing-and-journal-stats D7): a deterministic set of summarized runs over
// several missions, a pack and every trade shape (open, without R, liquidation, margin call, options).
function statsRuns() {
	const reasons = ['stop', 'target', 'manual', 'liquidation', 'trailing', 'margin_call', 'expiry', 'open'];
	const missions = ['m01-false-breakout', 'm06-thin-book', 'm04-options-buyer', 'csv-practice', 'm07-trend-trailing'];
	const findings = [['no_stop', 'bad'], ['oversized', 'warn'], ['no_plan', 'warn'], ['waited', 'info'], ['good_process_win', 'good'], ['funding_cost', 'warn']];
	const runs = [];
	for (let i = 0; i < 26; i++) {
		const trades = [];
		for (let k = 0; k < (i % 4) + 1; k++) {
			const j = i * 7 + k * 3;
			const reason = reasons[j % reasons.length];
			const rBp = reason === 'open' || j % 5 === 0 ? null : ((j * 3137) % 50000) - 22000;
			trades.push({
				net: rBp == null ? (j % 3) - 1 : rBp / 100,
				rBp,
				riskPct: j % 6 === 0 ? null : ((j * 37) % 400) / 100,
				reason,
				respected: j % 4 === 0 ? null : j % 3 !== 0,
				movedAway: j % 7 === 0 ? null : j % 3,
				option: j % 7 === 0
			});
		}
		const f = [findings[i % findings.length], findings[(i * 5) % findings.length], findings[(i + 1) % findings.length]];
		runs.push({
			runId: `r${i}`,
			missionId: missions[i % missions.length],
			createdAt: 1000 + i * 10 - (i % 3),
			summary: { v: 1, mission: missions[i % missions.length], status: i % 9 === 8 ? 'bankrupt' : 'finished', initial: 10000, net: 0, score: i % 5 === 0 ? null : 40 + i, trades, findings: f.map(([code, tone]) => ({ code, tone })) }
		});
	}
	return runs.sort((a, b) => a.createdAt - b.createdAt);
}

function journalStatsFixture(m, summary) {
	const { list, add } = cases();
	const runs = statsRuns();
	for (const r of [-3, -2, -1.99, -1, -0.5, 0, 0.01, 1, 1.5, 2, 2.0001, 7]) add('rBucket', [r], m.rBucket(r));
	for (const n of [0, 1, 4, 26]) add('coreStats', [runs.slice(0, n)], m.coreStats(runs.slice(0, n)));
	add('mistakes', [runs], m.mistakes(runs));
	add('byMission', [runs], m.byMission(runs));
	add('byStage', [runs], m.byStage(runs));
	for (const n of [3, 12, 26]) add('trend', [runs.slice(0, n)], m.trend(runs.slice(0, n)));
	for (const mission of [null, 'm06-thin-book']) {
		const s = m.journalStats(runs, mission);
		add('journalStats', [runs, mission], { trades: s.core.trades, runs: s.core.runs, tooFew: s.tooFew });
	}
	const trade = (p) => ({ n: 1, series: 0, side: 'long', entryStep: 1, exitStep: 5, qty: '1', entryAvg: '100', exitAvg: '98', pnl: '-2', fees: '0.1', funding: '0', net: '-150.25', bars: 4, reason: 'stop', plannedRisk: '150', rBp: -10017, planRespected: true, stopMovedAway: 1, ...p });
	const debriefs = [
		{ mission: 'm07-trend-trailing', status: 'finished', finalEquity: '9849.75', netPnl: '-150.25', score: { total: 64 }, findings: [{ code: 'no_plan', tone: 'warn' }], trades: [trade({}), trade({ series: 4, plannedRisk: '0', rBp: null, planRespected: null, stopMovedAway: undefined, reason: 'expiry' })] },
		{ mission: 'm01-false-breakout', status: 'bankrupt', finalEquity: '12.5', netPnl: '-9987.5', score: { total: null }, findings: [], trades: [trade({ reason: 'liquidation', plannedRisk: '600', stopMovedAway: undefined })] },
		{ mission: 'm01-false-breakout', status: 'active', finalEquity: '10000', netPnl: '0', score: { total: null }, findings: [], trades: [] }
	];
	for (const d of debriefs) add('summarize', [d], summary.summarize(d));
	return { helper: 'journalStats.ts', cases: list };
}

// `golden(name)`: an engine what-if golden fixture's expected result (real engine answers).
export async function buildFixtures(load, golden) {
	const i18n = await load('i18n.svelte.ts');
	const curriculum = await load('curriculum.ts');
	return {
		sizing: sizingFixture(await load('sizing.ts')),
		stops: stopsFixture(await load('stops.ts')),
		optionsMath: optionsFixture(await load('optionsMath.ts')),
		format: formatFixture(await load('format.ts')),
		hotkeys: hotkeysFixture(await load('hotkeys.ts')),
		advisor: await advisorFixture(await load('advisor.ts'), i18n, load),
		points: pointsFixture(await load('points.ts')),
		campaign: campaignFixture(await load('campaign.ts'), curriculum),
		curriculum: curriculumFixture(curriculum),
		audioRules: audioFixture(await load('audioRules.ts')),
		payoff: payoffFixture(await load('content/payoff.ts')),
		ticketSandbox: ticketSandboxFixture(await load('content/ticketSandbox.ts')),
		lessonRun: lessonRunFixture(await load('lessonRun.svelte.ts')),
		review: reviewFixture(await load('review.ts')),
		glossary: glossaryFixture(await load('content/glossary.ts')),
		strategies: strategiesFixture(await load('strategies.ts')),
		combos: combosFixture(await load('combos.ts')),
		comparison: comparisonFixture(await load('debrief.ts')),
		whatifLab: whatifLabFixture(await load('whatifLab.ts'), golden('whatif-condor'), golden('whatif-call')),
		journalStats: journalStatsFixture(await load('journalStats.ts'), await load('runSummary.ts')),
		orderRules: orderRulesFixture(await load('orderRules.ts'), await load('stops.ts')),
		heatmap: heatmapFixture(await load('heatmap.ts'), await load('portfolio.ts'), null, golden('whatif-heatmap'), [
			gameOf(),
			gameOf({ multiplier: '10', lot: '0.01', optLot: '1' })
		])
	};
}
