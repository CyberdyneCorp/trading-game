// AppViewModel's update offer: the state reported by the update service, and the only command that
// installs, which the menu's button sends (spec desktop-client "Updates": menu-only offer, installed on
// the next start, never without the player's click).
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

void AppViewModel::on_update(const update::UpdateState& state) {
  if (state == update_) return;
  update_ = state;
  touch();
}

void AppViewModel::install_update() {
  if (screen_ != Screen::Menu || updates_ == nullptr || update_.status != update::UpdateState::Status::Available) return;
  update_.status = update::UpdateState::Status::Downloading;
  updates_->install();
  touch();
}

}  // namespace tradelab::desktop
