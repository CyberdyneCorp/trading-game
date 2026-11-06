// Names of the shell screens' widgets and actions (menu, briefing, debrief, curriculum map, journal,
// Academy lesson, settings): shared by the screen viewmodels, the Raven XML views and the view glue.
// The headless test checks that each exists in the loaded views and that each action is bound.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names {

// ---- Menu (web MissionMenu.svelte + CampaignPanel.svelte) ----
inline constexpr std::string_view kMenuResumeRow = "menu-resume-row";
inline constexpr std::string_view kMenuResumeText = "menu-resume-text";
inline constexpr std::string_view kMenuResume = "menu-resume";
inline constexpr std::string_view kMenuUpdateRow = "menu-update-row";
inline constexpr std::string_view kMenuUpdateText = "menu-update-text";
inline constexpr std::string_view kMenuUpdateInstall = "menu-update-install";
inline constexpr std::string_view kMenuCurriculum = "menu-curriculum";
inline constexpr std::string_view kMenuJournal = "menu-journal";
inline constexpr std::string_view kMenuSettings = "menu-settings";
inline constexpr std::string_view kMenuDetailDone = "menu-detail-done";
inline constexpr std::string_view kCampaignTitle = "campaign-title";
inline constexpr std::string_view kCampaignNote = "campaign-note";
inline constexpr std::string_view kCampaignSteps = "campaign-steps";
inline constexpr std::string_view kCampaignProgress = "campaign-progress";
inline constexpr std::string_view kCampaignEnded = "campaign-ended";
inline constexpr std::string_view kCampaignStart = "campaign-start";
inline constexpr std::string_view kCampaignContinue = "campaign-continue";
inline constexpr std::string_view kCampaignEnd = "campaign-end";
inline constexpr std::string_view kCampaignConfirmRow = "campaign-confirm-row";
inline constexpr std::string_view kCampaignConfirm = "campaign-end-confirm";
inline constexpr std::string_view kCampaignEndYes = "campaign-end-yes";
inline constexpr std::string_view kCampaignEndNo = "campaign-end-no";
inline constexpr std::string_view kCampaignPlayed = "campaign-played";

// ---- Briefing (web Briefing.svelte) ----
inline constexpr std::string_view kBriefPermissions = "brief-permissions";
inline constexpr std::string_view kBriefNotAllowed = "brief-not-allowed";
inline constexpr std::string_view kBriefLimits = "brief-limits";
inline constexpr std::string_view kBriefMmr = "brief-mmr";
inline constexpr std::string_view kBriefTiers = "brief-tiers";
inline constexpr std::string_view kBriefMark = "brief-mark";
inline constexpr std::string_view kBriefCosts = "brief-costs";
inline constexpr std::string_view kBriefShocks = "brief-shocks";
inline constexpr std::string_view kBriefObjectivesTitle = "brief-objectives-title";
inline constexpr std::string_view kBriefObjectives = "brief-objectives";
inline constexpr std::string_view kBriefLocked = "brief-locked";
inline constexpr std::string_view kBriefRules = "brief-rules";
inline constexpr std::string_view kBriefTopic = "brief-topic";

// ---- Debrief (web Debrief.svelte and its panels) ----
inline constexpr std::string_view kDebriefMission = "debrief-mission";
inline constexpr std::string_view kDebriefTags = "debrief-tags";
inline constexpr std::string_view kDebriefCampaignPanel = "debrief-campaign-panel";
inline constexpr std::string_view kDebriefCampaign = "debrief-campaign";
inline constexpr std::string_view kDebriefCampaignNext = "debrief-campaign-next";
inline constexpr std::string_view kDebriefCampaignNew = "debrief-campaign-new";
inline constexpr std::string_view kDebriefReportPanel = "debrief-report-panel";
inline constexpr std::string_view kDebriefReport = "debrief-report";
inline constexpr std::string_view kDebriefReportRegimes = "debrief-report-regimes";
inline constexpr std::string_view kDebriefOutcomeLines = "debrief-outcome-lines";
inline constexpr std::string_view kDebriefDims = "debrief-dims";
inline constexpr std::string_view kDebriefScoreNote = "debrief-score-note";
inline constexpr std::string_view kDebriefRevealTitle = "debrief-reveal-title";
inline constexpr std::string_view kDebriefReveal = "debrief-reveal";
inline constexpr std::string_view kDebriefFindingsTitle = "debrief-findings-title";
inline constexpr std::string_view kDebriefFindings = "debrief-findings";
inline constexpr std::string_view kDebriefNextLesson = "debrief-next-lesson";
inline constexpr std::string_view kDebriefDisclaimer = "debrief-disclaimer";
inline constexpr std::string_view kDebriefLessons = "debrief-lessons";
inline constexpr std::string_view kDebriefDecisions = "debrief-decisions";
inline constexpr std::string_view kDebriefGreeksPanel = "debrief-greeks-panel";
inline constexpr std::string_view kDebriefGreeks = "debrief-greeks";
inline constexpr std::string_view kDebriefTradesTitle = "debrief-trades-title";
inline constexpr std::string_view kDebriefTrades = "debrief-trades";
inline constexpr std::string_view kDebriefTradeNotes = "debrief-trade-notes";
inline constexpr std::string_view kDebriefCalibration = "debrief-calibration";
inline constexpr std::string_view kDebriefAdvisorPanel = "debrief-advisor-panel";
inline constexpr std::string_view kDebriefAdvisor = "debrief-advisor";
inline constexpr std::string_view kDebriefRegimesPanel = "debrief-regimes-panel";
inline constexpr std::string_view kDebriefRegimeBand = "debrief-regime-band";
inline constexpr std::string_view kDebriefRegimes = "debrief-regimes";
inline constexpr std::string_view kDebriefRegimesNote = "debrief-regimes-note";
inline constexpr std::string_view kDebriefRegimesTitle = "debrief-regimes-title";
inline constexpr std::string_view kDebriefReleasesTitle = "debrief-releases-title";
inline constexpr std::string_view kDebriefReleasesPanel = "debrief-releases-panel";
inline constexpr std::string_view kDebriefReleases = "debrief-releases";
inline constexpr std::string_view kDebriefReleasesNote = "debrief-releases-note";
inline constexpr std::string_view kDebriefTimeline = "debrief-timeline";

// ---- Curriculum map (web CurriculumMap.svelte) ----
inline constexpr std::string_view kCurriculumTitle = "curriculum-title";
inline constexpr std::string_view kCurriculumBack = "curriculum-back";
inline constexpr std::string_view kCurriculumNote = "curriculum-note";
inline constexpr std::string_view kCurriculumProgress = "curriculum-progress";
inline constexpr std::string_view kCurriculumStages = "curriculum-stages";
inline constexpr std::string_view kCurriculumLessons = "curriculum-lessons";
inline constexpr std::string_view kCurriculumLessonStart = "curriculum-lesson-start";

// ---- Journal (web Journal.svelte + JournalEntries.svelte) ----
inline constexpr std::string_view kJournalTitle = "journal-title";
inline constexpr std::string_view kJournalBack = "journal-back";
inline constexpr std::string_view kJournalNote = "journal-note";
inline constexpr std::string_view kJournalFilterRow = "journal-filter-row";
inline constexpr std::string_view kJournalFilterLabel = "journal-filter-label";
inline constexpr std::string_view kJournalFilter = "journal-filter";
inline constexpr std::string_view kJournalEmpty = "journal-empty";
inline constexpr std::string_view kJournalRuns = "journal-runs";

// ---- Academy lesson (web LessonRunner.svelte + LessonChart.svelte) ----
inline constexpr std::string_view kLessonRunPanel = "lesson-run-panel";
inline constexpr std::string_view kLessonHead = "lesson-head";
inline constexpr std::string_view kLessonStep = "lesson-step";
inline constexpr std::string_view kLessonTitle = "lesson-title";
inline constexpr std::string_view kLessonChart = "lesson-chart";
inline constexpr std::string_view kLessonChartNote = "lesson-chart-note";
inline constexpr std::string_view kLessonBars = "lesson-bars";
inline constexpr std::string_view kLessonBody = "lesson-body";
inline constexpr std::array<std::string_view, 4> kLessonChoices = {"lesson-choice-0", "lesson-choice-1",
                                                                   "lesson-choice-2", "lesson-choice-3"};
inline constexpr std::string_view kLessonFeedback = "lesson-feedback";
inline constexpr std::string_view kLessonLeave = "lesson-leave";
inline constexpr std::string_view kLessonPrev = "lesson-prev";
inline constexpr std::string_view kLessonNext = "lesson-next";
inline constexpr std::string_view kLessonDonePanel = "lesson-done-panel";
inline constexpr std::string_view kLessonDoneTitle = "lesson-done-title";
inline constexpr std::string_view kLessonDoneName = "lesson-done-name";
inline constexpr std::string_view kLessonScore = "lesson-score";
inline constexpr std::string_view kLessonDoneNote = "lesson-done-note";
inline constexpr std::string_view kLessonFinish = "lesson-finish";

// ---- Settings (web MissionMenu settings + ShortcutSettings.svelte; desktop: CRT effects, text scale) ----
inline constexpr std::string_view kSettingsTitle = "settings-title";
inline constexpr std::string_view kSettingsBack = "settings-back";
inline constexpr std::string_view kSettingsLanguage = "settings-language";
inline constexpr std::string_view kSettingsLangPt = "settings-lang-pt";
inline constexpr std::string_view kSettingsLangEn = "settings-lang-en";
inline constexpr std::string_view kSettingsCrt = "settings-crt";
inline constexpr std::string_view kSettingsScanlines = "settings-crt-scanlines";
inline constexpr std::string_view kSettingsBloom = "settings-crt-bloom";
inline constexpr std::string_view kSettingsNoise = "settings-crt-noise";
inline constexpr std::string_view kSettingsCrtNote = "settings-crt-note";
inline constexpr std::string_view kSettingsTextScaleLabel = "settings-text-scale-label";
inline constexpr std::string_view kSettingsTextScale = "settings-text-scale";
inline constexpr std::string_view kSettingsPalette = "settings-palette";
inline constexpr std::string_view kSettingsKeysTitle = "settings-keys-title";
inline constexpr std::string_view kSettingsKeys = "settings-keys";
inline constexpr std::string_view kSettingsKeyChange = "settings-key-change";
inline constexpr std::string_view kSettingsKeyReset = "settings-key-reset";
inline constexpr std::string_view kSettingsKeyMessage = "settings-key-message";
inline constexpr std::string_view kSettingsAbout = "settings-about";

// ---- Shell: CRT overlay over every screen ----
inline constexpr std::string_view kCrtOverlay = "crt-overlay";

inline constexpr auto kShellWidgets = std::to_array<std::string_view>({
    kMenuResumeRow, kMenuResumeText, kMenuResume, kMenuUpdateRow, kMenuUpdateText, kMenuUpdateInstall, kMenuCurriculum, kMenuJournal, kMenuSettings, kMenuDetailDone,
    kCampaignTitle, kCampaignNote, kCampaignSteps, kCampaignProgress, kCampaignEnded, kCampaignStart, kCampaignContinue,
    kCampaignEnd, kCampaignConfirmRow, kCampaignConfirm, kCampaignEndYes, kCampaignEndNo, kCampaignPlayed,
    kBriefPermissions, kBriefNotAllowed, kBriefLimits, kBriefMmr, kBriefTiers, kBriefMark, kBriefCosts, kBriefShocks,
    kBriefObjectivesTitle, kBriefObjectives, kBriefLocked, kBriefRules, kBriefTopic, kDebriefMission, kDebriefTags,
    kDebriefCampaignPanel, kDebriefCampaign, kDebriefCampaignNext, kDebriefCampaignNew, kDebriefReportPanel,
    kDebriefReport, kDebriefReportRegimes, kDebriefOutcomeLines, kDebriefDims, kDebriefScoreNote, kDebriefRevealTitle,
    kDebriefReveal, kDebriefFindingsTitle, kDebriefFindings, kDebriefNextLesson, kDebriefDisclaimer, kDebriefLessons,
    kDebriefDecisions, kDebriefGreeksPanel, kDebriefGreeks, kDebriefTradesTitle, kDebriefTrades, kDebriefTradeNotes,
    kDebriefCalibration, kDebriefAdvisorPanel, kDebriefAdvisor, kDebriefRegimesPanel, kDebriefRegimeBand,
    kDebriefRegimes, kDebriefRegimesNote, kDebriefRegimesTitle, kDebriefReleasesTitle, kDebriefReleasesPanel, kDebriefReleases, kDebriefReleasesNote,
    kDebriefTimeline, kCurriculumTitle, kCurriculumBack, kCurriculumNote, kCurriculumProgress, kCurriculumStages,
    kCurriculumLessons, kCurriculumLessonStart, kJournalTitle, kJournalBack, kJournalNote, kJournalFilterRow,
    kJournalFilterLabel, kJournalFilter, kJournalEmpty, kJournalRuns, kLessonRunPanel, kLessonHead, kLessonStep,
    kLessonTitle, kLessonChart, kLessonChartNote, kLessonBars, kLessonBody, kLessonChoices[0], kLessonChoices[1],
    kLessonChoices[2], kLessonChoices[3], kLessonFeedback, kLessonLeave, kLessonPrev, kLessonNext, kLessonDonePanel,
    kLessonDoneTitle, kLessonDoneName, kLessonScore, kLessonDoneNote, kLessonFinish, kSettingsTitle, kSettingsBack,
    kSettingsLanguage, kSettingsLangPt, kSettingsLangEn, kSettingsCrt, kSettingsScanlines, kSettingsBloom,
    kSettingsNoise, kSettingsCrtNote, kSettingsTextScaleLabel, kSettingsTextScale, kSettingsPalette,
    kSettingsKeysTitle, kSettingsKeys, kSettingsKeyChange, kSettingsKeyReset, kSettingsKeyMessage, kSettingsAbout,
    kCrtOverlay});

// Actions ("tl." keeps them clear of Raven's built-in actions).
inline constexpr std::string_view kActResume = "tl.menu.resume";
inline constexpr std::string_view kActUpdateInstall = "tl.menu.updateInstall";
inline constexpr std::string_view kActOpenCurriculum = "tl.menu.curriculum";
inline constexpr std::string_view kActOpenJournal = "tl.menu.journal";
inline constexpr std::string_view kActOpenSettings = "tl.menu.settings";
inline constexpr std::string_view kActCampaignStart = "tl.campaign.start";
inline constexpr std::string_view kActCampaignContinue = "tl.campaign.continue";
inline constexpr std::string_view kActCampaignEnd = "tl.campaign.end";
inline constexpr std::string_view kActCampaignEndYes = "tl.campaign.endYes";
inline constexpr std::string_view kActCampaignEndNo = "tl.campaign.endNo";
inline constexpr std::string_view kActCampaignNext = "tl.campaign.next";
inline constexpr std::string_view kActBackToMenu = "tl.shell.menu";
inline constexpr std::string_view kActLessonStart = "tl.curriculum.lesson";
inline constexpr std::string_view kActLessonLeave = "tl.lesson.leave";
inline constexpr std::string_view kActLessonPrev = "tl.lesson.prev";
inline constexpr std::string_view kActLessonNext = "tl.lesson.next";
inline constexpr std::string_view kActLessonFinish = "tl.lesson.finish";
inline constexpr std::array<std::string_view, 4> kActLessonChoices = {"tl.lesson.choice0", "tl.lesson.choice1",
                                                                      "tl.lesson.choice2", "tl.lesson.choice3"};
inline constexpr std::string_view kActLangPt = "tl.settings.pt";
inline constexpr std::string_view kActLangEn = "tl.settings.en";
inline constexpr std::string_view kActKeyChange = "tl.settings.keyChange";
inline constexpr std::string_view kActKeyReset = "tl.settings.keyReset";

inline constexpr auto kShellActions = std::to_array<std::string_view>(
    {kActResume, kActUpdateInstall, kActOpenCurriculum, kActOpenJournal, kActOpenSettings, kActCampaignStart, kActCampaignContinue,
     kActCampaignEnd, kActCampaignEndYes, kActCampaignEndNo, kActCampaignNext, kActBackToMenu, kActLessonStart,
     kActLessonLeave, kActLessonPrev, kActLessonNext, kActLessonFinish, kActLessonChoices[0], kActLessonChoices[1],
     kActLessonChoices[2], kActLessonChoices[3], kActLangPt, kActLangEn, kActKeyChange, kActKeyReset});

}  // namespace tradelab::desktop::names
