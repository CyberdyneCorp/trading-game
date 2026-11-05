// Shapes of the JSON returned by the engine C ABI (engine/include/tradelab/c_api.h).
// Monetary fields are decimal strings of fixed-point values: display them, never do state math on them.

export type Decimal = string;
export type RunStatus = 'active' | 'finished' | 'bankrupt';
export type OrderSideName = 'buy' | 'sell';
export type OrderTypeName = 'market' | 'limit' | 'stop_market' | 'take_profit' | 'trailing_stop';

export interface MissionInfo {
	id: string;
	totalBars: number;
	warmupBars: number;
	leverageCap: number;
	allowShort: boolean;
}

export interface Contract {
	tickSize: Decimal;
	lotSize: Decimal;
	minQty: Decimal;
	multiplier: Decimal;
	maxLeverage: number;
	leverageCap: number;
	makerFee: Decimal;
	takerFee: Decimal;
	maintenanceMargin: Decimal;
	fundingInterval: number;
	allowShort: boolean;
	halfSpread: Decimal;
	slippage: Decimal;
	capacityFraction: Decimal;
}

/** [open, high, low, close, volume, synthetic] */
export type BarTuple = [Decimal, Decimal, Decimal, Decimal, Decimal, boolean];

export interface Account {
	wallet: Decimal;
	equity: Decimal;
	unrealized: Decimal;
	positionMargin: Decimal;
	orderMargin: Decimal;
	available: Decimal;
	notional: Decimal;
	mark: Decimal;
	liquidationPrice: Decimal | null;
	nextFundingRate: Decimal;
	nextFundingStep: number;
}

export interface Position {
	qty: Decimal; // signed: > 0 long, < 0 short
	entry: Decimal;
	margin: Decimal;
}

export interface OrderView {
	id: number;
	type: OrderTypeName;
	side: OrderSideName;
	qty: Decimal;
	filled: Decimal;
	price: Decimal;
	reduceOnly: boolean;
	status: string;
	placedStep: number;
	parentId: number;
	ocoGroup: number;
	trailDistance: Decimal;
	bracketStop: Decimal;
	bracketTp: Decimal;
	series: number;
}

export type EventTypeName =
	| 'order'
	| 'fill'
	| 'funding'
	| 'liquidation'
	| 'bankrupt'
	| 'run_finished'
	| 'leverage_changed'
	| 'option_settlement'
	| 'margin_call'
	| 'margin_adjusted';

export interface EngineEvent {
	seq: number;
	step: number;
	type: EventTypeName;
	orderId: number;
	/** Type of the order the event refers to; null for events without an order. */
	orderType: OrderTypeName | null;
	status: string;
	reason: string;
	side: OrderSideName;
	qty: Decimal;
	price: Decimal;
	requested: Decimal;
	amount: Decimal;
	pnl: Decimal;
	ambiguous: boolean;
	gap: boolean;
	capacityLimited: boolean;
	series: number;
	approximate: boolean;
}

export interface Totals {
	realizedPnl: Decimal;
	feesPaid: Decimal;
	fundingNet: Decimal;
	insuranceAbsorbed: Decimal;
	turnover: Decimal;
	peakEquity: Decimal;
	maxDrawdown: Decimal;
	peakLeverageBp: number;
	minLiqDistanceBp: number;
	liquidations: number;
	fills: number;
	ambiguousFills: number;
	optionCash: Decimal;
	optionRealized: Decimal;
}

export interface OptionQuoteView {
	id: number;
	kind: 'call' | 'put';
	strike: Decimal;
	listStep: number;
	expiryStep: number;
	listed: boolean;
	bid: Decimal;
	ask: Decimal;
	mid: Decimal;
	iv: Decimal;
	delta: Decimal;
	gamma: Decimal;
	theta: Decimal;
	vega: Decimal;
	ivFromRealized: boolean;
}

export interface OptionPositionView {
	series: number;
	qty: Decimal; // signed
	avgPremium: Decimal;
}

export interface OptionsView {
	enabled: boolean;
	allowShort: boolean;
	multiplier: Decimal;
	premiumTick: Decimal;
	lotSize: Decimal;
	longValue: Decimal;
	shortValue: Decimal;
	initialMargin: Decimal;
	maintenanceMargin: Decimal;
	chain: OptionQuoteView[];
	positions: OptionPositionView[];
}

export interface PatternHitView {
	step: number; // bar that completes the pattern
	id: string; // matches the cheat sheet ids
	bars: number;
	bias: -1 | 0 | 1;
}

export interface Indicators {
	version: number;
	sma20: (Decimal | null)[];
	ema50: (Decimal | null)[];
	rsi14: (Decimal | null)[];
	atr14: (Decimal | null)[];
	vwap: (Decimal | null)[];
	bbUpper: (Decimal | null)[];
	bbLower: (Decimal | null)[];
	macd: (Decimal | null)[];
	macdSignal: (Decimal | null)[];
	macdHist: (Decimal | null)[];
	plusDi14: (Decimal | null)[];
	minusDi14: (Decimal | null)[];
	adx14: (Decimal | null)[];
	patternVersion: number;
	patterns: PatternHitView[];
}

export interface NewsView {
	step: number;
	kind:
		| 'rate_decision'
		| 'regulation_rumor'
		| 'exchange_incident'
		| 'network_outage'
		| 'liquidity_inflow'
		| 'institutional_adoption'
		| 'rumor_denied';
	tone: number;
	credibility: number;
	expectation: number;
	actual: number;
	/** Delivered ahead of its public release by a fast wire (until `step`). */
	early: boolean;
}

/**
 * Start options of a run: advice allowance and intel perks (engine policy v11); campaign capital
 * carried in and the curriculum's leverage cap (policy v12; absent = the mission's own rules).
 */
export interface RunOptionsView {
	adviceAllowance: number;
	newsLead: number;
	calendar: boolean;
	/** Decimal string > 0. */
	startingBalance?: string;
	/** > 0 lowers the mission's leverage cap. */
	leverageCap?: number;
}

export const DEFAULT_RUN_OPTIONS: RunOptionsView = { adviceAllowance: 0, newsLead: 0, calendar: false };

export type AdviceStance = 'long' | 'short' | 'wait';

export type AdviceReasonCode =
	| 'regime_trend_up'
	| 'regime_trend_down'
	| 'regime_range'
	| 'rsi_extreme'
	| 'band_fade'
	| 'macd_momentum'
	| 'pattern'
	| 'news_priced'
	| 'news_early'
	| 'funding_crowded'
	| 'low_vol_squeeze';

/** One reason: `vote` > 0 long, < 0 short, 0 toward wait or a warning; `ref` = bar (pattern, news) or crowded side. */
export interface AdviceReasonView {
	code: AdviceReasonCode;
	vote: number;
	ref: number;
}

export interface AdviceView {
	step: number;
	stance: AdviceStance;
	confidenceBp: number;
	stop: Decimal; // "0" when waiting
	size: Decimal; // "0" when waiting or below the minimum order
	reasons: AdviceReasonView[];
}

export interface AdvisorState {
	version: number;
	allowance: number;
	left: number;
	newsLead: number;
	calendar: boolean;
	advice: AdviceView[];
}

export interface GameState {
	mission: string;
	alias: string;
	status: RunStatus;
	/** 'synthetic' for built-in missions; the pack mode ('historical', ...) for pack runs. */
	marketMode: string;
	/** Imported packs are known scenarios (spec scenario-packs "Reveal metadata"). */
	knownScenario: boolean;
	/** Bar interval in seconds: 3600 for the missions, the declared one for a pack (e.g. 900). */
	stepSeconds: number;
	step: number;
	lastDecisionStep: number;
	warmupBars: number;
	totalBars: number;
	initialBalance: Decimal;
	leverage: number;
	contract: Contract;
	bars: BarTuple[];
	account: Account;
	position: Position;
	orders: OrderView[];
	options: OptionsView;
	indicators: Indicators;
	news: NewsView[];
	/** Economic calendar perk: bars of upcoming scheduled releases (rate decisions), nothing else. */
	calendar: number[];
	calibration: { due: boolean; horizon: number; every: number; threshold: Decimal; answered: number };
	advisor: AdvisorState;
	events: EngineEvent[];
	totals: Totals;
	digest: string;
	eventsDigest: string;
}

export interface CommandResult {
	accepted: boolean;
	reason: string;
	orderId: number;
	warning: string | null;
}

export interface EngineFinding {
	code:
		| 'waited'
		| 'liquidated'
		| 'stop_beyond_liq'
		| 'no_stop'
		| 'gap_slippage'
		| 'funding_cost'
		| 'ambiguous'
		| 'capacity'
		| 'good_process_loss'
		| 'lucky_win'
		| 'no_plan'
		| 'oversized'
		| 'bankrupt'
		| 'margin_called'
		| 'theta_decay'
		| 'iv_crush'
		| 'good_process_win'
		| 'iv_rise'
		| 'wrote_options';
	tone: 'good' | 'warn' | 'bad' | 'info';
	step: number;
	count: number;
	price: Decimal;
	requested: Decimal;
	amount: Decimal;
}

export interface EngineTrade {
	n: number;
	/** 0 = perpetual; otherwise the option series (prices are premiums). */
	series: number;
	/** Option rows only. */
	kind?: 'call' | 'put';
	strike?: Decimal;
	expiryStep?: number;
	side: 'long' | 'short';
	entryStep: number;
	exitStep: number | null;
	qty: Decimal;
	entryAvg: Decimal;
	exitAvg: Decimal;
	pnl: Decimal;
	fees: Decimal;
	/** Funding received (+) or paid (-) while the trade was open; included in `net`. */
	funding: Decimal;
	net: Decimal;
	bars: number;
	reason: 'stop' | 'target' | 'manual' | 'liquidation' | 'open' | 'expiry' | 'margin_call';
	plannedRisk: Decimal;
	rBp: number | null;
	planRespected: boolean | null;
	/** Perpetual rows only: the trade's stop (stop order, else the plan's invalidation; 0 = none). */
	stop?: Decimal;
	/** Perpetual rows only: highest leverage the position carried (at entry or re-leveraged while open) and the liquidation price it implied (0 = none). */
	leverage?: number;
	liqPrice?: Decimal;
	stopBeyondLiq?: boolean;
	/** Highest leverage that would have kept the stop in front of liquidation (0 = not even 1x); null when it was in front. */
	safeLeverage?: number | null;
}

/** Approximate option P&L by Greek (engine); the residual holds spread and higher-order effects. */
export interface GreekAttribution {
	delta: Decimal;
	gamma: Decimal;
	theta: Decimal;
	vega: Decimal;
	residual: Decimal;
	total: Decimal;
}

export interface ScoreDimension {
	weight: number;
	score: number | null;
}

export type LessonCode = 'futures2' | 'risk' | 'futures3' | 'orders' | 'structure' | 'evaluation' | 'options';

export interface DebriefData {
	mission: string;
	alias: string;
	status: RunStatus;
	finalEquity: Decimal;
	buyAndHoldPnl: Decimal;
	flatPnl: Decimal;
	syntheticMarket: boolean;
	seed: number;
	startPrice: Decimal;
	endPrice: Decimal;
	score: {
		version: number;
		total: number | null;
		dims: { risk: ScoreDimension; plan: ScoreDimension; understanding: ScoreDimension; calibration: ScoreDimension };
	};
	findings: EngineFinding[];
	lesson: LessonCode;
	entries: number;
	netPnl: Decimal;
	calibration: { resolved: number; brierBp: number | null };
	trades: EngineTrade[];
	/** Spread + slippage paid against the reference price; already inside the fill prices. */
	spreadSlippage: Decimal;
	/** Option loss beyond the balance that the simulated fund covered (wallet floored at 0); usually 0. */
	writtenOff: Decimal;
	/** Null when no option was traded. */
	greeks: GreekAttribution | null;
	/** Engine-computed advisor points earned by this run (capped at 6). */
	advisorPointsEarned: number;
	/** Each piece of advice with its outcome 5 bars after the next open. */
	advice: { step: number; stance: AdviceStance; confidenceBp: number; outcome: 'right' | 'wrong' | 'wait' | 'open' }[];
	adviceHitBp: number | null;
	/** Pack runs only: what the masked market really was (never sent before the debrief). */
	knownScenario?: boolean;
	reveal?: PackReveal;
}

export interface PackReveal {
	asset: string;
	pair: string;
	venue: string;
	periodFrom: string;
	periodTo: string;
	source: string;
	transformation: string;
	gaps: number;
}

/** Engine CSV report (tl_csv_validate). Codes are stable; see engine/src/csv_import.cpp. */
export interface CsvReport {
	ok: boolean;
	rows: number;
	errors: { line: number; code: string }[];
	errorCount: number;
	gaps: { line: number; from: number; to: number; missing: number }[];
	gapCount: number;
	hasFunding: boolean;
	firstTime: number;
	lastTime: number;
	/** Set instead of the rest when the metadata itself is unusable. */
	metaError?: string;
}

/** What the player declares about a CSV (spec scenario-packs "Private CSV import"). */
export interface CsvMeta {
	asset: string;
	pair: string;
	venue: string;
	volumeUnit: 'base' | 'quote';
	intervalSeconds: number;
	timestampUnit: 's' | 'ms';
	leverageCap: number;
}

/** Pack summary from tl_pack_validate: identity and rules, never prices or the reveal. */
export interface PackSummary {
	scenarioId: string;
	mode: string;
	private: boolean;
	knownScenario: boolean;
	digest: string;
	signingKeyId: string;
	timeframe: string;
	totalBars: number;
	warmupBars: number;
	leverageCap: number;
	allowShort: boolean;
	gaps: number;
}

export interface PackCheck {
	ok: boolean;
	reason: string;
	detail: string;
	pack?: PackSummary;
}

export interface TrustedKey {
	id: string;
	key: string;
}

export const ORDER_TYPE_CODE: Record<OrderTypeName, number> = {
	market: 0,
	limit: 1,
	stop_market: 2,
	take_profit: 3,
	trailing_stop: 4
};
export const SIDE_CODE: Record<OrderSideName, number> = { buy: 0, sell: 1 };
