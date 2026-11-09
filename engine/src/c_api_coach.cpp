// tl_coach_review (add-practice-and-advanced-strategies design D17): the coach moments of a finished run.
// Report-only: refused while the run is active, and it never changes the run, its log or a digest.

#include <optional>

#include "c_api_run.hpp"
#include "json_out.hpp"
#include "tradelab/scoring.hpp"

namespace tradelab {
namespace {

using detail::JsonOut;

const char* limit_name(RiskLimitKind k) {
  switch (k) {
    case RiskLimitKind::Delta: return "delta";
    case RiskLimitKind::Gamma: return "gamma";
    case RiskLimitKind::Vega: return "vega";
    case RiskLimitKind::Theta: return "theta";
  }
  return "delta";
}

template <typename T>
void opt(JsonOut& j, const char* key, const std::optional<T>& v) {
  if (v) j.field(key, *v);
}

void write_evidence(JsonOut& j, const CoachEvidence& e) {
  j.key("evidence").begin_obj();
  opt(j, "qty", e.qty);
  opt(j, "entry", e.entry);
  opt(j, "close", e.close);
  opt(j, "sma", e.sma);
  opt(j, "level", e.level);
  opt(j, "stop", e.stop);
  opt(j, "from", e.from);
  opt(j, "to", e.to);
  opt(j, "price", e.price);
  opt(j, "risk", e.risk);
  opt(j, "budget", e.budget);
  opt(j, "loss", e.loss);
  opt(j, "plannedRisk", e.planned_risk);
  opt(j, "trade", e.trade);
  opt(j, "exitStep", e.exit_step);
  opt(j, "sizeRatioBp", e.size_ratio_bp);
  opt(j, "planned", e.planned);
  opt(j, "closes", e.closes);
  opt(j, "extensionAtr", e.extension_atr);
  opt(j, "avgBefore", e.avg_before);
  opt(j, "avgAfter", e.avg_after);
  opt(j, "riskBefore", e.risk_before);
  opt(j, "riskAfter", e.risk_after);
  opt(j, "planRisk", e.plan_risk);
  opt(j, "releaseStep", e.release_step);
  if (e.limit) j.field("limit", limit_name(*e.limit));
  opt(j, "limitBp", e.limit_bp);
  opt(j, "valueBp", e.value_bp);
  opt(j, "profitRBp", e.profit_r_bp);
  j.end_obj();
}

}  // namespace
}  // namespace tradelab

extern "C" {

const char* tl_coach_review(tl_run* run) {
  using namespace tradelab;  // NOLINT(google-build-using-namespace)
  if (run->state.status == RunStatus::Active) {
    run->buffer = "{\"error\":\"run_active\"}";
    return run->buffer.c_str();
  }
  detail::JsonOut j;
  j.begin_obj().field("mission", run->scenario.id).field("seed", run->state.seed).key("moments").begin_arr();
  for (const CoachMoment& m : coach_review(run->scenario, run->state)) {
    j.begin_obj().field("step", m.step).field("code", json_name(m.code)).field("tone", json_name(m.tone));
    write_evidence(j, m.evidence);
    j.field("lesson", coach_lesson(m.code)).field("glossary", coach_glossary(m.code)).end_obj();
  }
  j.end_arr().end_obj();
  run->buffer = j.text();
  return run->buffer.c_str();
}

}  // extern "C"
