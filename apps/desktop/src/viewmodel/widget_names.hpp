// Names shared by the viewmodel's bindings, the Raven XML views (Name="...") and the actions they fire
// (OnClicked="..."). The headless test checks that every one of them exists in the loaded views.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names {

// Screens (children of the root Switcher, in this order).
inline constexpr std::array<std::string_view, 19> kViews = {
    "views/menu.xml",       "views/briefing.xml", "views/game.xml",     "views/debrief.xml",
    "views/curriculum.xml", "views/journal.xml",  "views/lesson.xml",   "views/settings.xml",
    "views/tutorial.xml",   "views/catalog.xml",  "views/import.xml",   "views/academy.xml",
    "views/glossary.xml",   "views/lab.xml",      "views/stats.xml",    "views/drills.xml",
    "views/daily.xml",      "views/guess.xml",    "views/review.xml"};

// Widgets fed by AppViewModel::bindings().
inline constexpr std::string_view kMenuTitle = "menu-title";
inline constexpr std::string_view kMenuSubtitle = "menu-subtitle";
inline constexpr std::string_view kMenuNote = "menu-note";
inline constexpr std::string_view kMenuList = "menu-list";
inline constexpr std::string_view kMenuDetailTitle = "menu-detail-title";
inline constexpr std::string_view kMenuDetailTagline = "menu-detail-tagline";
inline constexpr std::string_view kMenuDetailTopic = "menu-detail-topic";
inline constexpr std::string_view kMenuDetailTags = "menu-detail-tags";
inline constexpr std::string_view kMenuStart = "menu-start";
inline constexpr std::string_view kMenuLanguage = "menu-language";
inline constexpr std::string_view kMenuDisclaimer = "menu-disclaimer";

inline constexpr std::string_view kBriefTitle = "brief-title";
inline constexpr std::string_view kBriefMission = "brief-mission";
inline constexpr std::string_view kBriefTagline = "brief-tagline";
inline constexpr std::string_view kBriefCapital = "brief-capital";
inline constexpr std::string_view kBriefInstrument = "brief-instrument";
inline constexpr std::string_view kBriefLeverage = "brief-leverage";
inline constexpr std::string_view kBriefMode = "brief-mode";
inline constexpr std::string_view kBriefGo = "brief-go";
inline constexpr std::string_view kBriefBack = "brief-back";

inline constexpr std::string_view kGameMarket = "game-market";
inline constexpr std::string_view kGameMission = "game-mission";
inline constexpr std::string_view kGameTurn = "game-turn";
inline constexpr std::string_view kGameChart = "game-chart";
inline constexpr std::string_view kGameAccount = "game-account";
inline constexpr std::string_view kGameQtyLabel = "game-qty-label";
inline constexpr std::string_view kGameQty = "game-qty";
inline constexpr std::string_view kGameBuy = "game-buy";
inline constexpr std::string_view kGameSell = "game-sell";
inline constexpr std::string_view kGameAdvance = "game-advance";
inline constexpr std::string_view kGameNotice = "game-notice";
inline constexpr std::string_view kGameMenu = "game-menu";

inline constexpr std::string_view kDebriefTitle = "debrief-title";
inline constexpr std::string_view kDebriefText = "debrief-text";
inline constexpr std::string_view kDebriefOutcome = "debrief-outcome";
inline constexpr std::string_view kDebriefPnl = "debrief-pnl";
inline constexpr std::string_view kDebriefPnlPct = "debrief-pnl-pct";
inline constexpr std::string_view kDebriefHold = "debrief-hold";
inline constexpr std::string_view kDebriefScore = "debrief-score";
inline constexpr std::string_view kDebriefMenu = "debrief-menu";
inline constexpr std::string_view kDebriefAgain = "debrief-again";

inline constexpr std::array<std::string_view, 41> kWidgets = {
    kMenuTitle,      kMenuSubtitle,    kMenuNote,        kMenuList,         kMenuDetailTitle, kMenuDetailTagline,
    kMenuDetailTopic, kMenuDetailTags, kMenuStart,       kMenuLanguage,     kMenuDisclaimer,  kBriefTitle,
    kBriefMission,   kBriefTagline,    kBriefCapital,    kBriefInstrument,  kBriefLeverage,   kBriefMode,
    kBriefGo,        kBriefBack,       kGameMarket,      kGameMission,      kGameTurn,        kGameChart,
    kGameAccount,    kGameQtyLabel,    kGameQty,         kGameBuy,          kGameSell,        kGameAdvance,
    kGameNotice,     kGameMenu,        kDebriefTitle,    kDebriefText,      kDebriefOutcome,  kDebriefPnl,
    kDebriefPnlPct,  kDebriefHold,     kDebriefScore,    kDebriefMenu,      kDebriefAgain};

// Actions (Raction_bind names); "tl." keeps them clear of Raven's built-in actions.
inline constexpr std::string_view kActStart = "tl.menu.start";
inline constexpr std::string_view kActLanguage = "tl.menu.language";
inline constexpr std::string_view kActBriefGo = "tl.brief.go";
inline constexpr std::string_view kActBriefBack = "tl.brief.back";
inline constexpr std::string_view kActAdvance = "tl.game.advance";
inline constexpr std::string_view kActBuy = "tl.game.buy";
inline constexpr std::string_view kActSell = "tl.game.sell";
inline constexpr std::string_view kActGameMenu = "tl.game.menu";
inline constexpr std::string_view kActDebriefMenu = "tl.debrief.menu";
inline constexpr std::string_view kActAgain = "tl.debrief.again";

inline constexpr std::array<std::string_view, 10> kActions = {kActStart,   kActLanguage, kActBriefGo,     kActBriefBack,
                                                              kActAdvance, kActBuy,      kActSell,        kActGameMenu,
                                                              kActDebriefMenu, kActAgain};

}  // namespace tradelab::desktop::names
