// AppViewModel: the Academy's options lab (web LabScreen.svelte, options-whatif spec "Lab in the
// Academy"): an illustrative market at 100 with legs the player edits, valued by the engine's run-less
// what-if (tl_option_whatif) on the simulation worker under price, time and IV shifts.
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

void AppViewModel::open_lab() {
  if (screen_ != Screen::Menu) return;
  academy_lab_.open_academy();
  screen_ = Screen::Lab;
  academy_lab_price_at_model();  // the call's premium is its model value, then the valuation
  touch();
}

void AppViewModel::academy_lab_changed() {
  const std::string request = academy_lab_.request();
  const auto ticket = academy_lab_.ticket.next(request);
  if (!ticket) return touch();
  const int t = *ticket;
  engine_.analyze(AnalysisKind::WhatIf, request, [alive = std::weak_ptr<bool>(alive_), self = this, t](const std::string& text) {
    if (alive.expired() || !self->academy_lab_.ticket.current(t)) return;
    self->academy_lab_.answer(text);
    self->touch();
  });
  touch();
}

void AppViewModel::academy_lab_price_at_model() {
  engine_.analyze(AnalysisKind::WhatIf, academy_lab_.pricing_request(), [alive = std::weak_ptr<bool>(alive_), self = this](const std::string& text) {
    if (alive.expired()) return;
    self->academy_lab_.priced(text);
    self->academy_lab_changed();
  });
}

}  // namespace tradelab::desktop
