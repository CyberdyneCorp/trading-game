#include "viewmodel/academy/lesson_lab.hpp"

#include "viewmodel/format.hpp"

namespace tradelab::desktop::academy {

std::vector<game::AcademyLeg> academy_legs(const std::vector<LessonOptionLeg>& legs) {
  std::vector<game::AcademyLeg> out;
  out.reserve(legs.size());
  for (const auto& l : legs) {
    out.push_back({game::leg_kind_from(l.kind).value_or(game::LegKind::Call), l.buy, l.strike, l.qty, l.days, l.iv_pct, l.entry});
  }
  return out;
}

std::vector<game::Slider> unlocked_sliders(const std::vector<std::string>& codes) {
  std::vector<game::Slider> out;
  for (const auto& c : codes) {
    for (const game::Slider s : game::kSliders) {
      if (game::slider_code(s) == c) out.push_back(s);
    }
  }
  return out;
}

void open_whatif(game::LabViewModel& lab, const WhatIfWidget& w) {
  lab.open_academy();
  lab.forward = w.forward;
  lab.academy_legs = academy_legs(w.legs);
  lab.unlocked = unlocked_sliders(w.unlocked);
}

std::optional<std::vector<game::AcademyLeg>> builder_academy_legs(const std::optional<std::vector<game::BuilderLeg>>& legs,
                                                                  const BuilderWidget& w) {
  if (!legs) return std::nullopt;
  std::vector<game::AcademyLeg> out;
  out.reserve(legs->size());
  for (const auto& l : *legs) {
    const bool perp = l.kind == game::LegKind::Perp;
    out.push_back({l.kind, l.buy, l.strike.value_or(0), l.qty, w.days, w.iv_pct, perp ? w.forward : 0});
  }
  return out;
}

std::vector<game::AcademyLeg> priced_legs(const std::vector<game::AcademyLeg>& legs, const game::WhatIfResult& priced) {
  std::vector<game::AcademyLeg> out = legs;
  for (std::size_t i = 0; i < out.size() && i < priced.legs.size(); ++i) {
    if (out[i].kind != game::LegKind::Perp) out[i].entry = fmt::num(game::to_fixed(fmt::num(priced.legs[i].value), 2));
  }
  return out;
}

// ---- LessonBuilder ----

void LessonBuilder::open(const BuilderWidget& w) {
  widget_ = w;
  tmpl_ = game::template_from(w.tmpl);
  short_ = false;
  clear();
}

void LessonBuilder::pick(game::Template t) {
  tmpl_ = t;
  clear();
}

void LessonBuilder::set_short(bool on) {
  if (!tmpl_ || !game::template_shortable(*tmpl_) || on == short_) return;
  short_ = on;
  clear();
}

void LessonBuilder::clear() {
  result_.reset();
  error_.clear();
}

std::optional<std::vector<game::AcademyLeg>> LessonBuilder::legs() const {
  if (!tmpl_) return std::nullopt;
  return builder_academy_legs(game::template_legs(*tmpl_, widget_.strikes, widget_.forward, 1, short_), widget_);
}

std::optional<std::string> LessonBuilder::pricing_request() const {
  const auto l = legs();
  if (!l) return std::nullopt;
  return game::academy_request(*l, {}, widget_.forward);
}

std::optional<std::string> LessonBuilder::priced(const std::string& text) {
  const auto l = legs();
  const auto a = game::whatif_from_json(text);
  if (!a.ok()) error_ = a.error;
  if (!l || !a.ok() || a.value->legs.size() != l->size()) return std::nullopt;
  return game::academy_request(priced_legs(*l, *a.value), {}, widget_.forward);
}

void LessonBuilder::answer(const std::string& text) {
  auto a = game::whatif_from_json(text);
  result_ = std::move(a.value);
  error_ = a.error;
}

LessonBuilderView LessonBuilder::view(const game::Texts& t) const {
  const Locale L = t.locale();
  LessonBuilderView v;
  for (const game::Template id : game::kTemplates) {
    v.templates.push_back({id, t.tr("strategy." + game::strategy_of(id, short_)), true, tmpl_ == id});
  }
  v.short_label = t.tr("builder.short");
  v.short_enabled = tmpl_ && game::template_shortable(*tmpl_);
  v.note = t.tr("academy.builder.note", {{"iv", game::js_number(widget_.iv_pct)}});
  if (!error_.empty()) {
    v.error = t.tr(error_ == "unsupported" ? "lab.unsupported" : "lab.error");
    return v;
  }
  if (!result_ || !result_->expiry) {
    v.pick = t.tr("academy.builder.pick");
    return v;
  }
  const game::WhatIfResult& r = *result_;
  v.has_result = true;
  v.analysis.emplace_back(t.tr("opt.breakeven"), game::breakevens_text(r, L));
  v.analysis.emplace_back(t.tr("lab.maxProfit"), game::max_profit_text(t, r));
  v.analysis.emplace_back(t.tr("lab.maxLoss"), game::max_loss_text(t, r));
  v.table = game::whatif_table(t, r, true);
  game::chart_texts(t, r, v.chart_summary, v.chart_legend);
  return v;
}

}  // namespace tradelab::desktop::academy
