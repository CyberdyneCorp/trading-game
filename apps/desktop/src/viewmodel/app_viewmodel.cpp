#include "viewmodel/app_viewmodel.hpp"

#include <utility>

#include "viewmodel/format.hpp"

namespace tradelab::desktop {

AppViewModel::AppViewModel(Catalog catalog, std::vector<MissionInfo> missions, EngineCommands& engine,
                           SeedSource seeds)
    : catalog_(std::move(catalog)), missions_(std::move(missions)), engine_(engine), seeds_(std::move(seeds)) {}

void AppViewModel::set_locale(Locale locale) {
  catalog_.set_locale(locale);
  touch();
}

void AppViewModel::toggle_locale() { set_locale(locale() == Locale::PtBR ? Locale::En : Locale::PtBR); }

std::string AppViewModel::tr(std::string_view key, const Params& params) const { return catalog_.tr(key, params); }

std::vector<std::string> AppViewModel::mission_titles() const {
  std::vector<std::string> out;
  out.reserve(missions_.size());
  for (const auto& m : missions_) out.push_back(mission_text(m.id, "title"));
  return out;
}

void AppViewModel::select(int index) {
  if (index < 0 || index >= static_cast<int>(missions_.size()) || index == selected_) return;
  selected_ = index;
  touch();
}

// One command at a time: the next waits for the worker's acknowledgement (no queue of stale clicks).
bool AppViewModel::send(const std::function<void()>& command) {
  if (in_flight_ > 0) return false;
  ++in_flight_;
  command();
  touch();
  return true;
}

void AppViewModel::start_selected() {
  if (missions_.empty() || screen_ != Screen::Menu) return;
  const std::string id = missions_[static_cast<std::size_t>(selected_)].id;
  if (send([&] { engine_.start(id, seeds_()); })) {
    start_pending_ = true;
    run_mission_ = id;
    notice_.clear();
  }
}

void AppViewModel::open_terminal() {
  if (screen_ != Screen::Briefing || !snap_) return;
  screen_ = Screen::Game;
  touch();
}

void AppViewModel::back_to_menu() {
  screen_ = Screen::Menu;
  notice_.clear();
  touch();
}

void AppViewModel::set_qty_text(std::string text) {
  if (text == qty_text_) return;
  qty_text_ = std::move(text);
  touch();
}

void AppViewModel::advance(int steps) {
  if (screen_ != Screen::Game || !snap_ || !snap_->active()) return;
  send([&] { engine_.advance(steps); });
}

void AppViewModel::buy() { send_order(Side::Buy); }

void AppViewModel::sell() { send_order(Side::Sell); }

void AppViewModel::send_order(Side side) {
  if (screen_ != Screen::Game || !snap_ || !snap_->active()) return;
  const std::string qty = fmt::qty_to_lot(fmt::parse_decimal(qty_text_), fmt::num(snap_->lot_size));
  if (qty == "0") {
    notice_ = tr("desktop.qtyInvalid");
    touch();
    return;
  }
  send([&] { engine_.market(side, qty); });
}

bool AppViewModel::handle_key(Key key, bool typing) {
  if (typing || screen_ != Screen::Game) return false;
  if (key == Key::Space) {
    advance(1);
    return true;
  }
  if (key == Key::ShiftSpace) {
    advance(5);
    return true;
  }
  return false;
}

void AppViewModel::play_again() {
  if (screen_ != Screen::Debrief) return;
  screen_ = Screen::Menu;
  start_selected();
}

void AppViewModel::on_snapshot(std::shared_ptr<const Snapshot> snap) {
  if (!snap) return;
  snap_ = std::move(snap);
  apply_results(*snap_);
  if (screen_ == Screen::Game && !snap_->active() && snap_->debrief) screen_ = Screen::Debrief;
  touch();
}

void AppViewModel::apply_results(const Snapshot& s) {
  for (const auto& r : s.results) {
    in_flight_ = in_flight_ > 0 ? in_flight_ - 1 : 0;
    apply_result(r);
  }
}

void AppViewModel::apply_result(const CommandResult& r) {
  if (r.kind == CommandKind::Start) {
    if (start_pending_ && r.accepted && screen_ == Screen::Menu) screen_ = Screen::Briefing;
    start_pending_ = false;
  }
  if (r.kind == CommandKind::Market) notice_ = r.accepted ? std::string() : "\xE2\x9C\x96 " + reason_text(r.reason);  // ✖
}

std::string AppViewModel::mission_text(const std::string& id, std::string_view field) const {
  return tr("mission." + id + "." + std::string(field));
}

std::string AppViewModel::reason_text(const std::string& code) const {
  const std::string key = "reason." + code;
  return catalog_.has(key) ? tr(key) : code;
}

ChartGeometry AppViewModel::chart(float width, float height) const {
  return snap_ ? chart_geometry(snap_->bars, width, height) : ChartGeometry{};
}

}  // namespace tradelab::desktop
