// Names of the widgets of wave 1 of add-practice-and-advanced-strategies in views/game.xml (time in
// force, "Protect with", the protective tabs, the planned-trailing notice, OCO links, the portfolio, the
// heatmaps, live combos and working limit legs), shared by the view glue and the headless tests.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names::wave1 {

// Perpetual ticket: time in force, "Protect with", the planned trailing stop.
inline constexpr std::string_view kTifLabel = "game-tif-label";
inline constexpr std::string_view kTif = "game-tif";
inline constexpr std::string_view kTifBarsRow = "game-tif-bars-row";
inline constexpr std::string_view kTifBarsLabel = "game-tif-bars-label";
inline constexpr std::string_view kTifBars = "game-tif-bars";
inline constexpr std::string_view kTifHint = "game-tif-hint";
inline constexpr std::string_view kProtectWith = "game-protect-with";
inline constexpr std::string_view kProtectWithLabel = "game-protect-with-label";
inline constexpr std::string_view kProtectFixed = "game-protect-fixed";
inline constexpr std::string_view kProtectTrail = "game-protect-trail";
inline constexpr std::string_view kPlanTrailBox = "game-plan-trail-box";
inline constexpr std::string_view kPlanTrailPrice = "game-plan-trail-price";
inline constexpr std::string_view kPlanTrailPct = "game-plan-trail-pct";
inline constexpr std::string_view kPlanTrail = "game-plan-trail";
inline constexpr std::string_view kPlanTrailNote = "game-plan-trail-note";
inline constexpr std::string_view kStopBox = "game-stop-box";
// Protective section: the tabs and their groups, the trailing rule.
inline constexpr std::string_view kTabs = "game-ptabs";
inline constexpr std::string_view kTabStop = "game-ptab-stop";
inline constexpr std::string_view kTabTrail = "game-ptab-trail";
inline constexpr std::string_view kTabTarget = "game-ptab-target";
inline constexpr std::string_view kStopGroup = "game-pstop-box";
inline constexpr std::string_view kTrailGroup = "game-ptrail-box";
inline constexpr std::string_view kTargetGroup = "game-ptp-box";
inline constexpr std::string_view kTrailRule = "game-trail-rule";
// The planned-trailing notice.
inline constexpr std::string_view kTrailNotice = "game-trail-notice";
inline constexpr std::string_view kTrailNoticeText = "game-trail-notice-text";
inline constexpr std::string_view kTrailNoticePlace = "game-trail-notice-place";
inline constexpr std::string_view kTrailNoticeDismiss = "game-trail-notice-dismiss";
// Option ticket: time in force.
inline constexpr std::string_view kOptTifLabel = "game-opt-tif-label";
inline constexpr std::string_view kOptTif = "game-opt-tif";
inline constexpr std::string_view kOptTifBarsRow = "game-opt-tif-bars-row";
inline constexpr std::string_view kOptTifBarsLabel = "game-opt-tif-bars-label";
inline constexpr std::string_view kOptTifBars = "game-opt-tif-bars";
inline constexpr std::string_view kOptTifHint = "game-opt-tif-hint";
// Orders panel: OCO links (each row also has "game-order-pick-<id>").
inline constexpr std::string_view kLinkRow = "game-orders-link-row";
inline constexpr std::string_view kLink = "game-orders-link";
inline constexpr std::string_view kLinkHint = "game-orders-link-hint";
inline constexpr std::string_view kOrderPickPrefix = "game-order-pick-";
// Working limit legs.
inline constexpr std::string_view kWorking = "game-working";
inline constexpr std::string_view kWorkingTitle = "game-working-title";
inline constexpr std::string_view kWorkingText = "game-working-text";
inline constexpr std::string_view kWorkingCancel = "game-working-cancel";
// Rail: the portfolio summary.
inline constexpr std::string_view kRailPortfolio = "game-rail-portfolio";
inline constexpr std::string_view kRailPortfolioTitle = "game-rail-portfolio-title";
inline constexpr std::string_view kRailPortfolioTotals = "game-rail-portfolio-totals";
inline constexpr std::string_view kRailPortfolioWarn = "game-rail-portfolio-warn";
inline constexpr std::string_view kRailPortfolioOpen = "game-rail-portfolio-open";
// Modal::Confirm.
inline constexpr std::string_view kConfirmTitle = "game-ask-title";
inline constexpr std::string_view kConfirmText = "game-ask-text";
inline constexpr std::string_view kConfirmNo = "game-ask-no";
inline constexpr std::string_view kConfirmYes = "game-ask-yes";
// Modal::Portfolio.
inline constexpr std::string_view kPortfolioTitle = "game-portfolio-title";
inline constexpr std::string_view kPortfolioEmpty = "game-portfolio-empty";
inline constexpr std::string_view kPortfolioRows = "game-portfolio-rows";
inline constexpr std::string_view kPortfolioPerUnit = "game-portfolio-per-unit";
inline constexpr std::string_view kPortfolioTotals = "game-portfolio-totals";
inline constexpr std::string_view kPortfolioLimitsTitle = "game-portfolio-limits-title";
inline constexpr std::string_view kPortfolioLimits = "game-portfolio-limits";
inline constexpr std::string_view kPortfolioWarnings = "game-portfolio-warnings";
inline constexpr std::string_view kPortfolioHedge = "game-portfolio-hedge";
inline constexpr std::string_view kPortfolioHedgeNote = "game-portfolio-hedge-note";
inline constexpr std::string_view kPortfolioClose = "game-portfolio-close";
// The heatmaps: "<prefix>-title", "-grid", "-table", "-focus", "-legend", "-view".
inline constexpr std::string_view kBuilderHeatmap = "game-builder-heatmap";
inline constexpr std::string_view kBuilderHeatmapToggle = "game-builder-heatmap-toggle";
inline constexpr std::string_view kPortfolioHeatmap = "game-portfolio-heatmap";
// Live combos (builder tab).
inline constexpr std::string_view kLive = "game-live";
inline constexpr std::string_view kLiveTitle = "game-live-title";
inline constexpr std::string_view kLiveCombos = "game-live-combos";
inline constexpr std::string_view kLivePanel = "game-live-panel";
inline constexpr std::string_view kLivePanelTitle = "game-live-panel-title";
inline constexpr std::string_view kLiveNone = "game-live-none";
inline constexpr std::string_view kLiveTo = "game-live-to";
inline constexpr std::string_view kLiveRecenter = "game-live-recenter";
inline constexpr std::string_view kLiveLimit = "game-live-limit";
inline constexpr std::string_view kLiveHeld = "game-live-held";
inline constexpr std::string_view kLiveHedge = "game-live-hedge";
inline constexpr std::string_view kLiveHedgeNone = "game-live-hedge-none";
inline constexpr std::string_view kLiveOrders = "game-live-orders";
inline constexpr std::string_view kLiveTotals = "game-live-totals";
inline constexpr std::string_view kLiveSend = "game-live-send";
inline constexpr std::string_view kLiveCancel = "game-live-cancel";

inline constexpr std::array<std::string_view, 73> kRequired = {
    kTifLabel,         kTif,                 kTifBarsRow,         kTifBarsLabel,     kTifBars,          kTifHint,
    kProtectWith,      kProtectWithLabel,    kProtectFixed,       kProtectTrail,     kPlanTrailBox,     kPlanTrailPrice,
    kPlanTrailPct,     kPlanTrail,           kPlanTrailNote,      kStopBox,          kTabs,             kTabStop,
    kTabTrail,         kTabTarget,           kStopGroup,          kTrailGroup,       kTargetGroup,      kTrailRule,
    kTrailNotice,      kTrailNoticeText,     kTrailNoticePlace,   kTrailNoticeDismiss, kOptTifLabel,    kOptTif,
    kOptTifBarsRow,    kOptTifBarsLabel,     kOptTifBars,         kOptTifHint,       kLinkRow,          kLink,
    kLinkHint,         kWorking,             kWorkingTitle,       kWorkingText,      kWorkingCancel,    kRailPortfolio,
    kRailPortfolioTitle, kRailPortfolioTotals, kRailPortfolioWarn, kRailPortfolioOpen, kConfirmTitle,   kConfirmText,
    kConfirmNo,        kConfirmYes,          kPortfolioTitle,     kPortfolioEmpty,   kPortfolioRows,    kPortfolioPerUnit,
    kPortfolioTotals,  kPortfolioLimitsTitle, kPortfolioLimits,   kPortfolioWarnings, kPortfolioHedge,  kPortfolioHedgeNote,
    kPortfolioClose,   kBuilderHeatmap,      kBuilderHeatmapToggle, kLive,           kLiveTitle,        kLiveCombos,
    kLivePanel,        kLiveTo,              kLiveRecenter,       kLiveLimit,        kLiveHeld,         kLiveSend,
    kLiveCancel};

}  // namespace tradelab::desktop::names::wave1
