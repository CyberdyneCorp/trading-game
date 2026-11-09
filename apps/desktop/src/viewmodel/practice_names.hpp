// Names of the practice formats' widgets and actions (add-practice-and-advanced-strategies D15-D19): the
// menu's practice row, the Drills, Daily challenge, Guess and Coach review screens, the debrief's coach
// button, daily tag, retry and mistake report, and the terminal's banners. Shared by the screen
// viewmodels (screens/practice.cpp), the Raven XML views and the view glue; the headless test checks
// each exists and is bound.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names {

// ---- Menu: the practice formats (MissionMenu.svelte practice-formats) ----
inline constexpr std::string_view kMenuPracticeTitle = "menu-practice-title";
inline constexpr std::string_view kMenuPracticeNote = "menu-practice-note";
inline constexpr std::string_view kMenuDrills = "menu-drills";
inline constexpr std::string_view kMenuDaily = "menu-daily";
inline constexpr std::string_view kMenuGuess = "menu-guess";

// ---- Daily drills (DrillScreen.svelte) ----
inline constexpr std::string_view kDrillsTitle = "drills-title";
inline constexpr std::string_view kDrillsBack = "drills-back";
inline constexpr std::string_view kDrillsIntroBox = "drills-intro-box";
inline constexpr std::string_view kDrillsIntro = "drills-intro";
inline constexpr std::string_view kDrillsTimer = "drills-timer";
inline constexpr std::string_view kDrillsReviewNote = "drills-review-note";
inline constexpr std::string_view kDrillsStart = "drills-start";
inline constexpr std::string_view kDrillsAskBox = "drills-ask-box";
inline constexpr std::string_view kDrillsPosition = "drills-position";
inline constexpr std::string_view kDrillsClock = "drills-clock";
inline constexpr std::string_view kDrillsPrompt = "drills-prompt";
inline constexpr std::string_view kDrillsChoices = "drills-choices";
inline constexpr std::array<std::string_view, 2> kDrillsChoice = {"drills-choice-0", "drills-choice-1"};
inline constexpr std::string_view kDrillsInputRow = "drills-input-row";
inline constexpr std::string_view kDrillsAnswerLabel = "drills-answer-label";
inline constexpr std::string_view kDrillsInput = "drills-input";
inline constexpr std::string_view kDrillsCheck = "drills-check";
inline constexpr std::string_view kDrillsFeedback = "drills-feedback";
inline constexpr std::string_view kDrillsExplain = "drills-explain";
inline constexpr std::string_view kDrillsNext = "drills-next";
inline constexpr std::string_view kDrillsEndBox = "drills-end-box";
inline constexpr std::string_view kDrillsEndTitle = "drills-end-title";
inline constexpr std::string_view kDrillsTimeUp = "drills-time-up";
inline constexpr std::string_view kDrillsFirstTry = "drills-first-try";
inline constexpr std::string_view kDrillsResults = "drills-results";
inline constexpr std::string_view kDrillsLessons = "drills-lessons";
inline constexpr std::string_view kDrillsLesson = "drills-lesson";
inline constexpr std::string_view kDrillsAgain = "drills-again";

// ---- Daily challenge (DailyScreen.svelte) ----
inline constexpr std::string_view kDailyTitle = "daily-title";
inline constexpr std::string_view kDailyBack = "daily-back";
inline constexpr std::string_view kDailyIntro = "daily-intro";
inline constexpr std::string_view kDailyToday = "daily-today";
inline constexpr std::string_view kDailyMission = "daily-mission";
inline constexpr std::string_view kDailySeed = "daily-seed";
inline constexpr std::string_view kDailyRecord = "daily-record";
inline constexpr std::string_view kDailyPlay = "daily-play";
inline constexpr std::string_view kDailyHistoryTitle = "daily-history-title";
inline constexpr std::string_view kDailyEmpty = "daily-empty";
inline constexpr std::string_view kDailyHistory = "daily-history";

// ---- Guess rounds (GuessScreen.svelte) ----
inline constexpr std::string_view kGuessTitle = "guess-title";
inline constexpr std::string_view kGuessBack = "guess-back";
inline constexpr std::string_view kGuessIntroBox = "guess-intro-box";
inline constexpr std::string_view kGuessIntro = "guess-intro";
inline constexpr std::string_view kGuessStart = "guess-start";
inline constexpr std::string_view kGuessStatus = "guess-status";
inline constexpr std::string_view kGuessChartBox = "guess-chart-box";
inline constexpr std::string_view kGuessMarket = "guess-market";
inline constexpr std::string_view kGuessChart = "guess-chart";
inline constexpr std::string_view kGuessCommitBox = "guess-commit-box";
inline constexpr std::string_view kGuessClose = "guess-close";
inline constexpr std::string_view kGuessDecisionLabel = "guess-decision-label";
inline constexpr std::string_view kGuessDecision = "guess-decision";
inline constexpr std::string_view kGuessPLabel = "guess-p-label";
inline constexpr std::string_view kGuessP = "guess-p";
inline constexpr std::string_view kGuessLevels = "guess-levels";
inline constexpr std::string_view kGuessInvLabel = "guess-inv-label";
inline constexpr std::string_view kGuessInv = "guess-inv";
inline constexpr std::string_view kGuessTargetLabel = "guess-target-label";
inline constexpr std::string_view kGuessTarget = "guess-target";
inline constexpr std::string_view kGuessProblem = "guess-problem";
inline constexpr std::string_view kGuessCommit = "guess-commit";
inline constexpr std::string_view kGuessRevealBox = "guess-reveal-box";
inline constexpr std::string_view kGuessRevealTitle = "guess-reveal-title";
inline constexpr std::string_view kGuessReveal = "guess-reveal";
inline constexpr std::string_view kGuessNext = "guess-next";
inline constexpr std::string_view kGuessRounds = "guess-rounds";
inline constexpr std::string_view kGuessTableTitle = "guess-table-title";
inline constexpr std::string_view kGuessTable = "guess-table";
inline constexpr std::string_view kGuessTableLocked = "guess-table-locked";

// ---- Coach review over the debrief (CoachReview.svelte) ----
inline constexpr std::string_view kReviewTitle = "review-title";
inline constexpr std::string_view kReviewClose = "review-close";
inline constexpr std::string_view kReviewIntro = "review-intro";
inline constexpr std::string_view kReviewStatus = "review-status";
inline constexpr std::string_view kReviewMomentBox = "review-moment-box";
inline constexpr std::string_view kReviewPosition = "review-position";
inline constexpr std::string_view kReviewTone = "review-tone";
inline constexpr std::string_view kReviewChart = "review-chart";
inline constexpr std::string_view kReviewText = "review-text";
inline constexpr std::string_view kReviewLesson = "review-lesson";
inline constexpr std::string_view kReviewTerm = "review-term";
inline constexpr std::string_view kReviewPrev = "review-prev";
inline constexpr std::string_view kReviewRetry = "review-retry";
inline constexpr std::string_view kReviewNext = "review-next";
inline constexpr std::string_view kReviewReveal = "review-reveal";

// ---- Debrief and terminal parts ----
inline constexpr std::string_view kDebriefDaily = "debrief-daily";
inline constexpr std::string_view kDebriefOpenReview = "debrief-open-review";
inline constexpr std::string_view kDebriefRetryBox = "debrief-retry-box";
inline constexpr std::string_view kDebriefRetryLabel = "debrief-retry-label";
inline constexpr std::string_view kDebriefRetryPick = "debrief-retry-pick";
inline constexpr std::string_view kDebriefRetry = "debrief-retry";
inline constexpr std::string_view kDebriefMistakePanel = "debrief-mistake-panel";
inline constexpr std::string_view kDebriefMistake = "debrief-mistake";
inline constexpr std::string_view kDebriefMistakeTable = "debrief-mistake-table";
inline constexpr std::string_view kDebriefMistakeNote = "debrief-mistake-note";
inline constexpr std::string_view kGameMistakeBanner = "game-mistake-banner";
inline constexpr std::string_view kGameDailyBanner = "game-daily-banner";

inline constexpr auto kPracticeWidgets = std::to_array<std::string_view>({
    kMenuPracticeTitle, kMenuPracticeNote, kMenuDrills, kMenuDaily, kMenuGuess, kDrillsTitle, kDrillsBack, kDrillsIntroBox,
    kDrillsIntro, kDrillsTimer, kDrillsReviewNote, kDrillsStart, kDrillsAskBox, kDrillsPosition, kDrillsClock, kDrillsPrompt,
    kDrillsChoices, kDrillsChoice[0], kDrillsChoice[1], kDrillsInputRow, kDrillsAnswerLabel, kDrillsInput, kDrillsCheck,
    kDrillsFeedback, kDrillsExplain, kDrillsNext, kDrillsEndBox, kDrillsEndTitle, kDrillsTimeUp, kDrillsFirstTry, kDrillsResults,
    kDrillsLessons, kDrillsLesson, kDrillsAgain, kDailyTitle, kDailyBack, kDailyIntro, kDailyToday, kDailyMission, kDailySeed,
    kDailyRecord, kDailyPlay, kDailyHistoryTitle, kDailyEmpty, kDailyHistory, kGuessTitle, kGuessBack, kGuessIntroBox, kGuessIntro,
    kGuessStart, kGuessStatus, kGuessChartBox, kGuessMarket, kGuessChart, kGuessCommitBox, kGuessClose, kGuessDecisionLabel,
    kGuessDecision, kGuessPLabel, kGuessP, kGuessLevels, kGuessInvLabel, kGuessInv, kGuessTargetLabel, kGuessTarget, kGuessProblem,
    kGuessCommit, kGuessRevealBox, kGuessRevealTitle, kGuessReveal, kGuessNext, kGuessRounds, kGuessTableTitle, kGuessTable,
    kGuessTableLocked, kReviewTitle, kReviewClose, kReviewIntro, kReviewStatus, kReviewMomentBox, kReviewPosition, kReviewTone,
    kReviewChart, kReviewText, kReviewLesson, kReviewTerm, kReviewPrev, kReviewRetry, kReviewNext, kReviewReveal, kDebriefDaily,
    kDebriefOpenReview, kDebriefRetryBox, kDebriefRetryLabel, kDebriefRetryPick, kDebriefRetry, kDebriefMistakePanel,
    kDebriefMistake, kDebriefMistakeTable, kDebriefMistakeNote, kGameMistakeBanner, kGameDailyBanner});

// Actions.
inline constexpr std::string_view kActOpenDrills = "tl.menu.drills";
inline constexpr std::string_view kActOpenDaily = "tl.menu.daily";
inline constexpr std::string_view kActOpenGuess = "tl.menu.guess";
inline constexpr std::string_view kActDrillsStart = "tl.drills.start";
inline constexpr std::array<std::string_view, 2> kActDrillsChoice = {"tl.drills.choice0", "tl.drills.choice1"};
inline constexpr std::string_view kActDrillsCheck = "tl.drills.check";
inline constexpr std::string_view kActDrillsNext = "tl.drills.next";
inline constexpr std::string_view kActDrillsLesson = "tl.drills.lesson";
inline constexpr std::string_view kActDailyPlay = "tl.daily.play";
inline constexpr std::string_view kActGuessStart = "tl.guess.start";
inline constexpr std::string_view kActGuessCommit = "tl.guess.commit";
inline constexpr std::string_view kActReviewOpen = "tl.review.open";
inline constexpr std::string_view kActReviewClose = "tl.review.close";
inline constexpr std::string_view kActReviewPrev = "tl.review.prev";
inline constexpr std::string_view kActReviewNext = "tl.review.next";
inline constexpr std::string_view kActReviewRetry = "tl.review.retry";
inline constexpr std::string_view kActReviewLesson = "tl.review.lesson";
inline constexpr std::string_view kActReviewTerm = "tl.review.term";
inline constexpr std::string_view kActReviewReveal = "tl.review.reveal";
inline constexpr std::string_view kActDebriefRetry = "tl.debrief.retry";

inline constexpr auto kPracticeActions = std::to_array<std::string_view>(
    {kActOpenDrills, kActOpenDaily, kActOpenGuess, kActDrillsStart, kActDrillsChoice[0], kActDrillsChoice[1], kActDrillsCheck,
     kActDrillsNext, kActDrillsLesson, kActDailyPlay, kActGuessStart, kActGuessCommit, kActReviewOpen, kActReviewClose,
     kActReviewPrev, kActReviewNext, kActReviewRetry, kActReviewLesson, kActReviewTerm, kActReviewReveal, kActDebriefRetry});

}  // namespace tradelab::desktop::names
