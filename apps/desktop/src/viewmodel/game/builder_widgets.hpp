// Names of the strategy builder's, the combo notice's and the terminal lab's widgets in views/game.xml
// (and the Academy lab screen's in views/lab.xml), shared by the view glue and the headless test.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names::builder {

inline constexpr std::string_view kTab = "game-tab-builder";
inline constexpr std::string_view kTitle = "game-builder-title";
inline constexpr std::string_view kLabPositions = "game-builder-lab-positions";
inline constexpr std::string_view kNone = "game-builder-none";
inline constexpr std::string_view kBody = "game-builder-body";
inline constexpr std::string_view kExpiryLabel = "game-builder-expiry-label";
inline constexpr std::string_view kExpiry = "game-builder-expiry";
inline constexpr std::string_view kQtyLabel = "game-builder-qty-label";
inline constexpr std::string_view kQty = "game-builder-qty";
inline constexpr std::string_view kShort = "game-builder-short";
inline constexpr std::string_view kTemplatePrefix = "game-builder-tpl-";  // 0..6, in kTemplates order
inline constexpr std::string_view kWriteLocked = "game-builder-write-locked";
inline constexpr std::string_view kCaption = "game-builder-caption";
inline constexpr std::string_view kLegs = "game-builder-legs";
inline constexpr std::string_view kAddLeg = "game-builder-add-leg";
inline constexpr std::string_view kUseLong = "game-builder-use-long";
inline constexpr std::string_view kProblem = "game-builder-problem";
inline constexpr std::string_view kNoAnalysis = "game-builder-no-analysis";
inline constexpr std::string_view kPreview = "game-builder-preview";
inline constexpr std::string_view kTotals = "game-builder-totals";
inline constexpr std::string_view kAnalysis = "game-builder-analysis";
inline constexpr std::string_view kPayoff = "game-builder-payoff";
inline constexpr std::string_view kTable = "game-builder-table";
inline constexpr std::string_view kOpenLab = "game-builder-open-lab";
inline constexpr std::string_view kLegging = "game-builder-legging";
inline constexpr std::string_view kSubmit = "game-builder-submit";

// The incomplete-combo notice (book panel).
inline constexpr std::string_view kNotice = "game-combo-notice";
inline constexpr std::string_view kNoticeTitle = "game-combo-title";
inline constexpr std::string_view kNoticeOpen = "game-combo-open";
inline constexpr std::string_view kNoticeCancelled = "game-combo-cancelled";
inline constexpr std::string_view kNoticeRisk = "game-combo-risk";
inline constexpr std::string_view kNoticeLegging = "game-combo-legging";
inline constexpr std::string_view kNoticeKeep = "game-combo-keep";
inline constexpr std::string_view kNoticeClose = "game-combo-close";

// The dialogs: the terminal lab and the confirmation to close an incomplete combo's open legs.
inline constexpr std::string_view kLabPrefix = "game-lab-";
inline constexpr std::string_view kLabTitle = "game-lab-title";
inline constexpr std::string_view kLabClose = "game-lab-close";
inline constexpr std::string_view kCloseTitle = "game-combo-close-title";
inline constexpr std::string_view kCloseText = "game-combo-close-text";
inline constexpr std::string_view kCloseNo = "game-combo-close-no";
inline constexpr std::string_view kCloseYes = "game-combo-close-yes";

inline constexpr std::array<std::string_view, 34> kRequired = {
    kTab,      kTitle,         kLabPositions, kNone,       kBody,     kExpiryLabel,  kExpiry,       kQtyLabel,       kQty,
    kShort,    kWriteLocked,   kCaption,      kLegs,       kAddLeg,   kUseLong,      kProblem,      kNoAnalysis,     kPreview,
    kTotals,   kAnalysis,      kPayoff,       kTable,      kOpenLab,  kLegging,      kSubmit,       kNotice,         kNoticeKeep,
    kNoticeClose, kLabTitle,   kLabClose,     kCloseTitle, kCloseText, kCloseNo,     kCloseYes};

// The Academy lab screen (views/lab.xml).
inline constexpr std::string_view kScreenPrefix = "lab-";
inline constexpr std::string_view kScreenTitle = "lab-title";
inline constexpr std::string_view kScreenBack = "lab-back";
inline constexpr std::string_view kScreenIntro = "lab-intro";
inline constexpr std::string_view kScreenTag = "lab-illustrative";
inline constexpr std::string_view kScreenLegsCaption = "lab-legs-caption";
inline constexpr std::string_view kScreenLegs = "lab-legs";
inline constexpr std::string_view kScreenAddOption = "lab-add-option";
inline constexpr std::string_view kScreenAddPerp = "lab-add-perp";
inline constexpr std::string_view kScreenPriceModel = "lab-price-model";
inline constexpr std::string_view kMenuLab = "menu-lab";

}  // namespace tradelab::desktop::names::builder
