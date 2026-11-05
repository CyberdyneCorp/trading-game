// Names of the terminal's widgets in resources/views/game.xml (beyond the foundation's names in
// viewmodel/widget_names.hpp) and of the actions they fire. Shared by the view, the XML and the tests;
// the headless test checks that every one exists and every action is bound.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names::game {

// Top bar and chart header
inline constexpr std::string_view kInterval = "game-interval";
inline constexpr std::string_view kAuto = "game-auto";
inline constexpr std::string_view kAutoSpeed = "game-auto-speed";
inline constexpr std::string_view kAutoEvents = "game-auto-events";
inline constexpr std::string_view kHelp = "game-help";
inline constexpr std::string_view kPatterns = "game-patterns";
inline constexpr std::string_view kLessons = "game-lessons";
inline constexpr std::string_view kTutorial = "game-tutorial";
inline constexpr std::string_view kPerks = "game-perks";
inline constexpr std::string_view kSound = "game-sound";
inline constexpr std::string_view kJournal = "game-journal";
inline constexpr std::string_view kReadout = "game-readout";
inline constexpr std::string_view kSma = "game-sma";
inline constexpr std::string_view kEma = "game-ema";
inline constexpr std::string_view kVwap = "game-vwap";
inline constexpr std::string_view kBb = "game-bb";
inline constexpr std::string_view kShowPatterns = "game-show-patterns";
inline constexpr std::string_view kOsc = "game-osc";
inline constexpr std::string_view kTableToggle = "game-table-toggle";
inline constexpr std::string_view kChartStatus = "game-chart-status";
inline constexpr std::string_view kPatternLine = "game-pattern-line";
inline constexpr std::string_view kTableBox = "game-table-box";
inline constexpr std::string_view kTable = "game-table";
inline constexpr std::string_view kTablePage = "game-table-page";
inline constexpr std::string_view kTablePrev = "game-table-prev";
inline constexpr std::string_view kTableNext = "game-table-next";
inline constexpr std::string_view kTableLast = "game-table-last";

// Rail
inline constexpr std::string_view kRailEquity = "game-rail-equity";
inline constexpr std::string_view kRailPnl = "game-rail-pnl";
inline constexpr std::string_view kRailAccount = "game-rail-account";
inline constexpr std::string_view kRailPosition = "game-rail-position";
inline constexpr std::string_view kRailDetails = "game-rail-details";
inline constexpr std::string_view kRailLiq = "game-rail-liq";
inline constexpr std::string_view kRailLiqWarning = "game-rail-liq-warning";
inline constexpr std::string_view kMarginBox = "game-margin-box";
inline constexpr std::string_view kMarginAmount = "game-margin-amount";
inline constexpr std::string_view kMarginAdd = "game-margin-add";
inline constexpr std::string_view kMarginRemove = "game-margin-remove";
inline constexpr std::string_view kRailOptions = "game-rail-options";
inline constexpr std::string_view kRailFunding = "game-rail-funding";
inline constexpr std::string_view kRailObjectives = "game-rail-objectives";

// Perpetual ticket (game-qty is the amount field of the sizing mode; game-buy/game-sell pick the side)
inline constexpr std::string_view kTabs = "game-tabs";
inline constexpr std::string_view kTabPerp = "game-tab-perp";
inline constexpr std::string_view kTabOptions = "game-tab-options";
inline constexpr std::string_view kTicketSwitch = "game-ticket-switch";
inline constexpr std::string_view kShortLocked = "game-short-locked";
inline constexpr std::string_view kType = "game-type";
inline constexpr std::string_view kLevLabel = "game-lev-label";
inline constexpr std::string_view kLeverage = "game-leverage";
inline constexpr std::string_view kPriceBox = "game-price-box";
inline constexpr std::string_view kPriceLabel = "game-price-label";
inline constexpr std::string_view kPrice = "game-price";
inline constexpr std::string_view kSizeBy = "game-size-by";
inline constexpr std::string_view kStop = "game-stop";
inline constexpr std::string_view kStopPick = "game-stop-pick";
inline constexpr std::string_view kStopHint = "game-stop-hint";
inline constexpr std::string_view kTp = "game-tp";
inline constexpr std::string_view kTpPick = "game-tp-pick";
inline constexpr std::string_view kTpHint = "game-tp-hint";
inline constexpr std::string_view kNotes = "game-notes";
inline constexpr std::string_view kEstimate = "game-estimate";
inline constexpr std::string_view kBeyondConfirm = "game-beyond-confirm";
inline constexpr std::string_view kSubmit = "game-submit";
inline constexpr std::string_view kProtectBox = "game-protect";
inline constexpr std::string_view kUnprotected = "game-unprotected";
inline constexpr std::string_view kProtStop = "game-pstop";
inline constexpr std::string_view kProtStopPick = "game-pstop-pick";
inline constexpr std::string_view kProtStopHint = "game-pstop-hint";
inline constexpr std::string_view kProtTp = "game-ptp";
inline constexpr std::string_view kProtTpPick = "game-ptp-pick";
inline constexpr std::string_view kProtTpHint = "game-ptp-hint";
inline constexpr std::string_view kTrailByPrice = "game-trail-price";
inline constexpr std::string_view kTrailByPct = "game-trail-pct";
inline constexpr std::string_view kTrail = "game-trail";
inline constexpr std::string_view kTrailHint = "game-trail-hint";
inline constexpr std::string_view kProtBeyond = "game-pbeyond";
inline constexpr std::string_view kProtBeyondConfirm = "game-pbeyond-confirm";
inline constexpr std::string_view kProtect = "game-protect-submit";
inline constexpr std::string_view kClose = "game-close";
// Preset chips: "<field>-chip-<i>" for game-stop (5), game-tp (3), game-pstop (5), game-ptp (3),
// game-trail (3 ATR in price mode, 4 percents in percent mode).

// Option ticket and chain
inline constexpr std::string_view kOptSeries = "game-opt-series";
inline constexpr std::string_view kOptBuy = "game-opt-buy";
inline constexpr std::string_view kOptSell = "game-opt-sell";
inline constexpr std::string_view kOptQty = "game-opt-qty";
inline constexpr std::string_view kOptType = "game-opt-type";
inline constexpr std::string_view kOptLimit = "game-opt-limit";
inline constexpr std::string_view kOptEstimate = "game-opt-estimate";
inline constexpr std::string_view kOptNotes = "game-opt-notes";
inline constexpr std::string_view kOptPayoff = "game-opt-payoff";
inline constexpr std::string_view kOptSubmit = "game-opt-submit";
inline constexpr std::string_view kChainBox = "game-chain-box";
inline constexpr std::string_view kChain = "game-chain";

// Orders, advisor, news, feed
inline constexpr std::string_view kOrders = "game-orders";
inline constexpr std::string_view kOrdersNone = "game-orders-none";
inline constexpr std::string_view kAdvisorBox = "game-advisor";
inline constexpr std::string_view kAdvPoints = "game-adv-points";
inline constexpr std::string_view kAdvAsk = "game-adv-ask";
inline constexpr std::string_view kAdvRead = "game-adv-read";
inline constexpr std::string_view kAdvApply = "game-adv-apply";
inline constexpr std::string_view kAdvReasons = "game-adv-reasons";
inline constexpr std::string_view kNews = "game-news";
inline constexpr std::string_view kFeed = "game-feed";

// Dialogs (children of game-modal-switch, in Modal order from Plan)
inline constexpr std::string_view kModal = "game-modal";
inline constexpr std::string_view kModalSwitch = "game-modal-switch";
inline constexpr std::string_view kPlanHeadline = "game-plan-headline";
inline constexpr std::string_view kPlanThesis = "game-plan-thesis";
inline constexpr std::string_view kPlanInvalidation = "game-plan-inv";
inline constexpr std::string_view kPlanTarget = "game-plan-target";
inline constexpr std::string_view kPlanLoseBox = "game-plan-lose-box";
inline constexpr std::string_view kPlanLose = "game-plan-lose";
inline constexpr std::string_view kPlanConfidence = "game-plan-conf";
inline constexpr std::string_view kPlanConfidenceLabel = "game-plan-conf-label";
inline constexpr std::string_view kPlanCancel = "game-plan-cancel";
inline constexpr std::string_view kPlanSkip = "game-plan-skip";
inline constexpr std::string_view kPlanSave = "game-plan-save";
inline constexpr std::string_view kCalQuestion = "game-cal-question";
inline constexpr std::string_view kCalPct = "game-cal-pct";
inline constexpr std::string_view kCalPctLabel = "game-cal-pct-label";
inline constexpr std::string_view kCalSkip = "game-cal-skip";
inline constexpr std::string_view kCalAnswer = "game-cal-answer";
inline constexpr std::string_view kConfirmText = "game-confirm-text";
inline constexpr std::string_view kConfirmNo = "game-confirm-no";
inline constexpr std::string_view kConfirmYes = "game-confirm-yes";
inline constexpr std::string_view kHelpRows = "game-help-rows";
inline constexpr std::string_view kHelpMessage = "game-help-message";
inline constexpr std::string_view kHelpReset = "game-help-reset";
inline constexpr std::string_view kHelpOk = "game-help-ok";
inline constexpr std::string_view kJournalLines = "game-journal-lines";
inline constexpr std::string_view kJournalOk = "game-journal-ok";
inline constexpr std::string_view kPatternName = "game-pattern-name";
inline constexpr std::string_view kPatternBody = "game-pattern-body";
inline constexpr std::string_view kPatternOk = "game-pattern-ok";
inline constexpr std::string_view kLessonRows = "game-lesson-rows";
inline constexpr std::string_view kLessonClose = "game-lesson-close";
inline constexpr std::string_view kTutorialStep = "game-tutorial-step";
inline constexpr std::string_view kTutorialTitle = "game-tutorial-title";
inline constexpr std::string_view kTutorialBody = "game-tutorial-body";
inline constexpr std::string_view kTutorialPrev = "game-tutorial-prev";
inline constexpr std::string_view kTutorialNext = "game-tutorial-next";
inline constexpr std::string_view kTutorialSkip = "game-tutorial-skip";
inline constexpr std::string_view kPerkRows = "game-perk-rows";
inline constexpr std::string_view kPerkClose = "game-perk-close";

inline constexpr std::array<std::string_view, 21> kRequired = {
    kReadout, kTable, kRailEquity, kRailLiq, kType, kLeverage, kSizeBy, kStop, kTp, kEstimate, kSubmit,
    kOptSubmit, kChain, kOrders, kAdvAsk, kNews, kFeed, kModal, kPlanThesis, kCalPct, kHelpRows};

}  // namespace tradelab::desktop::names::game
