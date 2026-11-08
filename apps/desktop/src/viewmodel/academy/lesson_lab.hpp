// The engine-valued widgets of a lesson step (add-academy-and-strategies D3; web LessonWhatIf.svelte and
// LessonBuilder.svelte, whatifLab.ts builderAcademyLegs / pricedLegs): the what-if lab on the step's
// fixed legs with only its unlocked sliders, and the strategy builder's templates on a synthetic chain
// of one expiry, each leg priced at the engine's model value (tl_option_whatif, to the cent) and then
// analysed by the engine. Nothing is sent and no run is read; every number shown is the engine's.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/content.hpp"
#include "viewmodel/game/builder_vm.hpp"
#include "viewmodel/game/lab_vm.hpp"
#include "viewmodel/game/strategies.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::academy {

// The step's legs as the lab's Academy legs, and its unlocked sliders.
std::vector<game::AcademyLeg> academy_legs(const std::vector<LessonOptionLeg>& legs);
std::vector<game::Slider> unlocked_sliders(const std::vector<std::string>& codes);
// The what-if widget's lab: the fixed legs on the widget's market, every slider at zero.
void open_whatif(game::LabViewModel& lab, const WhatIfWidget& w);

// whatifLab.ts builderAcademyLegs: a template's legs on the synthetic chain (one expiry, one IV),
// entries unset (the perpetual at the forward); nullopt when the chain lacks a strike it needs.
std::optional<std::vector<game::AcademyLeg>> builder_academy_legs(const std::optional<std::vector<game::BuilderLeg>>& legs,
                                                                  const BuilderWidget& w);
// whatifLab.ts pricedLegs: each option's entry at the engine's model value, rounded to the cent.
std::vector<game::AcademyLeg> priced_legs(const std::vector<game::AcademyLeg>& legs, const game::WhatIfResult& priced);

struct LessonBuilderView {
  std::vector<game::TemplateButton> templates;
  std::string short_label;
  bool short_enabled = false;
  std::string note;   // the model and its IV
  std::string error;  // a localized failure, "" when none
  std::string pick;   // the hint before a template is picked, "" once a result shows
  std::vector<std::pair<std::string, std::string>> analysis;
  std::vector<game::Line> table;
  std::string chart_summary, chart_legend;
  bool has_result = false;
};

class LessonBuilder {
 public:
  // A builder step opened: its template (if the step names one), long side, no result yet.
  void open(const BuilderWidget& w);
  void pick(game::Template t);
  void set_short(bool on);

  [[nodiscard]] const std::optional<game::Template>& tmpl() const { return tmpl_; }
  [[nodiscard]] bool short_side() const { return short_; }
  // The current template's legs on the chain; nullopt with no template or a missing strike.
  [[nodiscard]] std::optional<std::vector<game::AcademyLeg>> legs() const;
  // The request that prices the legs at the model (no shift, entries unset); nullopt when none.
  [[nodiscard]] std::optional<std::string> pricing_request() const;
  // The pricing answer: the analysis request with those prices as the entries, or nullopt (an error
  // or a stale answer: the legs changed since).
  std::optional<std::string> priced(const std::string& text);
  void answer(const std::string& text);
  void clear();
  // Each pick or side change takes a ticket; replies to an older one are dropped.
  int next_ticket() { return ++ticket_; }
  [[nodiscard]] bool current(int ticket) const { return ticket == ticket_; }

  [[nodiscard]] const std::optional<game::WhatIfResult>& result() const { return result_; }
  [[nodiscard]] LessonBuilderView view(const game::Texts& t) const;

 private:
  BuilderWidget widget_;
  std::optional<game::Template> tmpl_;
  bool short_ = false;
  std::optional<game::WhatIfResult> result_;
  std::string error_;
  int ticket_ = 0;
};

}  // namespace tradelab::desktop::academy
