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

export interface Indicators {
	version: number;
	sma20: (Decimal | null)[];
	ema50: (Decimal | null)[];
	rsi14: (Decimal | null)[];
	atr14: (Decimal | null)[];
}

export interface GameState {
	mission: string;
	alias: string;
	status: RunStatus;
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
}

export const ORDER_TYPE_CODE: Record<OrderTypeName, number> = {
	market: 0,
	limit: 1,
	stop_market: 2,
	take_profit: 3,
	trailing_stop: 4
};
export const SIDE_CODE: Record<OrderSideName, number> = { buy: 0, sell: 1 };
