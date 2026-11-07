// Names of the Academy's widgets and actions (add-academy-and-strategies tasks D3, D6, D7, D8): the
// lesson widgets and questions, the Academy and glossary screens, the menu's review prompt, the
// curriculum and debrief links and the guided first trade's coach marks. Shared by the screen
// viewmodels, the Raven XML views and the view glue; the headless test checks each exists and is bound.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names {

// ---- Lesson widgets and questions (web LessonTicket / LessonTable / LessonPayoff / LessonQuestion) ----
inline constexpr std::string_view kLessonLevels = "lesson-levels";  // the chart's labeled lines, as text
inline constexpr std::string_view kLessonPayoffBox = "lesson-payoff-box";
inline constexpr std::string_view kLessonPayoffCaption = "lesson-payoff-caption";
inline constexpr std::string_view kLessonPayoffLegs = "lesson-payoff-legs";
inline constexpr std::string_view kLessonPayoff = "lesson-payoff";
inline constexpr std::string_view kLessonPayoffAnalytics = "lesson-payoff-analytics";
inline constexpr std::string_view kLessonPayoffNote = "lesson-payoff-note";
inline constexpr std::string_view kLessonTableBox = "lesson-table-box";
inline constexpr std::string_view kLessonTableCaption = "lesson-table-caption";
inline constexpr std::string_view kLessonTable = "lesson-table";
inline constexpr std::string_view kLessonTicketBox = "lesson-ticket-box";
inline constexpr std::string_view kSandboxCaption = "lesson-sandbox-caption";
inline constexpr std::string_view kSandboxFixed = "lesson-sandbox-fixed";
inline constexpr std::string_view kSandboxSideRow = "lesson-sandbox-side-row";
inline constexpr std::string_view kSandboxBuy = "lesson-sandbox-buy";
inline constexpr std::string_view kSandboxSell = "lesson-sandbox-sell";
inline constexpr std::string_view kSandboxQtyLabel = "lesson-sandbox-qty-label";
inline constexpr std::string_view kSandboxQty = "lesson-sandbox-qty";
inline constexpr std::string_view kSandboxLevLabel = "lesson-sandbox-lev-label";
inline constexpr std::string_view kSandboxLeverage = "lesson-sandbox-leverage";
inline constexpr std::string_view kSandboxStopLabel = "lesson-sandbox-stop-label";
inline constexpr std::string_view kSandboxStop = "lesson-sandbox-stop";
inline constexpr std::string_view kSandboxTargetRow = "lesson-sandbox-target-row";
inline constexpr std::string_view kSandboxTargetLabel = "lesson-sandbox-target-label";
inline constexpr std::string_view kSandboxTarget = "lesson-sandbox-target";
inline constexpr std::string_view kSandboxOut = "lesson-sandbox-out";
inline constexpr std::string_view kSandboxWarn = "lesson-sandbox-warn";
inline constexpr std::string_view kSandboxNote = "lesson-sandbox-note";
inline constexpr std::string_view kLessonNumberRow = "lesson-number-row";
inline constexpr std::string_view kLessonNumberLabel = "lesson-number-label";
inline constexpr std::string_view kLessonNumber = "lesson-number";
inline constexpr std::string_view kLessonNumberUnit = "lesson-number-unit";
inline constexpr std::string_view kLessonCheck = "lesson-check";
inline constexpr std::string_view kLessonSetRow = "lesson-set-row";
inline constexpr std::string_view kLessonSetCheck = "lesson-set-check";
inline constexpr std::string_view kLessonSetHint = "lesson-set-hint";
inline constexpr std::string_view kLessonTermsRow = "lesson-terms-row";
inline constexpr std::string_view kLessonTermsLabel = "lesson-terms-label";
inline constexpr std::array<std::string_view, 5> kLessonTerms = {"lesson-term-0", "lesson-term-1", "lesson-term-2", "lesson-term-3",
                                                                 "lesson-term-4"};
inline constexpr std::string_view kLessonTermHelp = "lesson-term-help";  // the focused term's definition
inline constexpr std::string_view kLessonDoneReview = "lesson-done-review";

// ---- Academy screen (web Academy.svelte) ----
inline constexpr std::string_view kAcademyTitle = "academy-title";
inline constexpr std::string_view kAcademyGlossary = "academy-glossary";
inline constexpr std::string_view kAcademyBack = "academy-back";
inline constexpr std::string_view kAcademyIntro = "academy-intro";
inline constexpr std::string_view kAcademyDueBox = "academy-due-box";
inline constexpr std::string_view kAcademyDueTitle = "academy-due-title";
inline constexpr std::string_view kAcademyDue = "academy-due";
inline constexpr std::string_view kAcademyReviewStart = "academy-review-start";
inline constexpr std::string_view kAcademyLessons = "academy-lessons";
inline constexpr std::string_view kAcademyDetail = "academy-detail";
inline constexpr std::string_view kAcademyStart = "academy-start";

// ---- Glossary screen (web Glossary.svelte + GlossaryList.svelte) ----
inline constexpr std::string_view kGlossaryTitle = "glossary-title";
inline constexpr std::string_view kGlossaryBack = "glossary-back";
inline constexpr std::string_view kGlossaryNote = "glossary-note";
inline constexpr std::string_view kGlossarySearchLabel = "glossary-search-label";
inline constexpr std::string_view kGlossarySearch = "glossary-search";
inline constexpr std::string_view kGlossaryCount = "glossary-count";
inline constexpr std::string_view kGlossaryList = "glossary-list";
inline constexpr std::string_view kGlossaryEntry = "glossary-entry";
inline constexpr std::string_view kGlossaryPick = "glossary-pick";
inline constexpr std::string_view kGlossaryTerm = "glossary-term";
inline constexpr std::string_view kGlossaryStage = "glossary-stage";
inline constexpr std::string_view kGlossaryShort = "glossary-short";
inline constexpr std::string_view kGlossaryLong = "glossary-long";
inline constexpr std::string_view kGlossaryExample = "glossary-example";
inline constexpr std::string_view kGlossaryAliases = "glossary-aliases";
inline constexpr std::string_view kGlossaryRelatedRow = "glossary-related-row";
inline constexpr std::string_view kGlossaryRelatedLabel = "glossary-related-label";
inline constexpr std::array<std::string_view, 5> kGlossaryRelated = {"glossary-related-0", "glossary-related-1", "glossary-related-2",
                                                                     "glossary-related-3", "glossary-related-4"};

// ---- Menu, curriculum, briefing and debrief links ----
inline constexpr std::string_view kMenuAcademy = "menu-academy";
inline constexpr std::string_view kMenuGlossary = "menu-glossary";
inline constexpr std::string_view kMenuReviewRow = "menu-review-row";
inline constexpr std::string_view kMenuReviewText = "menu-review-text";
inline constexpr std::string_view kMenuReviewOpen = "menu-review-open";
inline constexpr std::string_view kMenuReviewDismiss = "menu-review-dismiss";
inline constexpr std::string_view kCurriculumAcademy = "curriculum-academy";
inline constexpr std::string_view kBriefTour = "brief-tour";
inline constexpr std::string_view kDebriefOpenLesson = "debrief-open-lesson";
inline constexpr std::string_view kDebriefTermHelp = "debrief-term-help";

// ---- Guided first trade (web CoachMarks.svelte): the callout over the terminal and in the debrief ----
inline constexpr std::string_view kGameCoachLayer = "game-coach-layer";
inline constexpr std::string_view kGameCoachOutline = "game-coach-outline";
inline constexpr std::string_view kGameCoach = "game-coach";
inline constexpr std::string_view kGameCoachStep = "game-coach-step";
inline constexpr std::string_view kGameCoachTitle = "game-coach-title";
inline constexpr std::string_view kGameCoachBody = "game-coach-body";
inline constexpr std::string_view kGameCoachSkip = "game-coach-skip";
inline constexpr std::string_view kGameCoachNext = "game-coach-next";
inline constexpr std::string_view kGameHelpTour = "game-help-tour";
inline constexpr std::string_view kGameTermHelp = "game-term-help";  // the focused term's definition
// The terminal widget a coach step's target names (coach.json `target`; web `data-coach`).
constexpr std::string_view coach_anchor(std::string_view target) {
  if (target == "chart") return "game-panel-chart";
  if (target == "ticket") return "game-panel-ticket";
  if (target == "stop") return "game-stop";
  if (target == "risk") return "game-estimate";
  if (target == "advance") return "game-advance";
  return "debrief-findings";
}
inline constexpr std::string_view kDebriefCoach = "debrief-coach";
inline constexpr std::string_view kDebriefCoachStep = "debrief-coach-step";
inline constexpr std::string_view kDebriefCoachTitle = "debrief-coach-title";
inline constexpr std::string_view kDebriefCoachBody = "debrief-coach-body";
inline constexpr std::string_view kDebriefCoachSkip = "debrief-coach-skip";
inline constexpr std::string_view kDebriefCoachNext = "debrief-coach-next";

inline constexpr auto kAcademyWidgets = std::to_array<std::string_view>({
    kLessonLevels, kLessonPayoffBox, kLessonPayoffCaption, kLessonPayoffLegs, kLessonPayoff, kLessonPayoffAnalytics,
    kLessonPayoffNote, kLessonTableBox, kLessonTableCaption, kLessonTable, kLessonTicketBox, kSandboxCaption, kSandboxFixed,
    kSandboxSideRow, kSandboxBuy, kSandboxSell, kSandboxQtyLabel, kSandboxQty, kSandboxLevLabel, kSandboxLeverage,
    kSandboxStopLabel, kSandboxStop, kSandboxTargetRow, kSandboxTargetLabel, kSandboxTarget, kSandboxOut, kSandboxWarn,
    kSandboxNote, kLessonNumberRow, kLessonNumberLabel, kLessonNumber, kLessonNumberUnit, kLessonCheck, kLessonSetRow,
    kLessonSetCheck, kLessonSetHint, kLessonTermsRow, kLessonTermsLabel, kLessonTerms[0], kLessonTerms[1], kLessonTerms[2],
    kLessonTerms[3], kLessonTerms[4], kLessonTermHelp, kLessonDoneReview, kAcademyTitle, kAcademyGlossary, kAcademyBack,
    kAcademyIntro, kAcademyDueBox, kAcademyDueTitle, kAcademyDue, kAcademyReviewStart, kAcademyLessons, kAcademyDetail,
    kAcademyStart, kGlossaryTitle, kGlossaryBack, kGlossaryNote, kGlossarySearchLabel, kGlossarySearch, kGlossaryCount,
    kGlossaryList, kGlossaryEntry, kGlossaryPick, kGlossaryTerm, kGlossaryStage, kGlossaryShort, kGlossaryLong,
    kGlossaryExample, kGlossaryAliases, kGlossaryRelatedRow, kGlossaryRelatedLabel, kGlossaryRelated[0], kGlossaryRelated[1],
    kGlossaryRelated[2], kGlossaryRelated[3], kGlossaryRelated[4], kMenuAcademy, kMenuGlossary, kMenuReviewRow,
    kMenuReviewText, kMenuReviewOpen, kMenuReviewDismiss, kCurriculumAcademy, kBriefTour, kDebriefOpenLesson,
    kDebriefTermHelp, kGameCoachLayer, kGameCoachOutline, kGameCoach, kGameCoachStep, kGameCoachTitle, kGameCoachBody,
    kGameCoachSkip, kGameCoachNext, kGameHelpTour, kGameTermHelp, kDebriefCoach, kDebriefCoachStep, kDebriefCoachTitle,
    kDebriefCoachBody, kDebriefCoachSkip, kDebriefCoachNext});

// ---- Actions ----
inline constexpr std::string_view kActLessonCheck = "tl.lesson.check";
inline constexpr std::string_view kActLessonSetCheck = "tl.lesson.checkSet";
inline constexpr std::string_view kActSandboxBuy = "tl.lesson.sandboxBuy";
inline constexpr std::string_view kActSandboxSell = "tl.lesson.sandboxSell";
inline constexpr std::array<std::string_view, 5> kActLessonTerms = {"tl.lesson.term0", "tl.lesson.term1", "tl.lesson.term2",
                                                                    "tl.lesson.term3", "tl.lesson.term4"};
inline constexpr std::string_view kActOpenAcademy = "tl.academy.open";
inline constexpr std::string_view kActOpenGlossary = "tl.glossary.open";
inline constexpr std::string_view kActAcademyStart = "tl.academy.start";
inline constexpr std::string_view kActAcademyReview = "tl.academy.review";
inline constexpr std::string_view kActGlossaryBack = "tl.glossary.back";
inline constexpr std::array<std::string_view, 5> kActGlossaryRelated = {"tl.glossary.related0", "tl.glossary.related1",
                                                                        "tl.glossary.related2", "tl.glossary.related3",
                                                                        "tl.glossary.related4"};
inline constexpr std::string_view kActReviewDismiss = "tl.menu.reviewDismiss";
inline constexpr std::string_view kActDebriefLesson = "tl.debrief.lesson";
inline constexpr std::string_view kActBriefTour = "tl.brief.tour";
inline constexpr std::string_view kActCoachNext = "tl.coach.next";
inline constexpr std::string_view kActCoachSkip = "tl.coach.skip";
inline constexpr std::string_view kActHelpTour = "tl.game.helpTour";

inline constexpr auto kAcademyActions = std::to_array<std::string_view>(
    {kActLessonCheck, kActLessonSetCheck, kActSandboxBuy, kActSandboxSell, kActLessonTerms[0], kActLessonTerms[1],
     kActLessonTerms[2], kActLessonTerms[3], kActLessonTerms[4], kActOpenAcademy, kActOpenGlossary, kActAcademyStart,
     kActAcademyReview, kActGlossaryBack, kActGlossaryRelated[0], kActGlossaryRelated[1], kActGlossaryRelated[2],
     kActGlossaryRelated[3], kActGlossaryRelated[4], kActReviewDismiss, kActDebriefLesson, kActBriefTour, kActCoachNext,
     kActCoachSkip, kActHelpTour});

}  // namespace tradelab::desktop::names
