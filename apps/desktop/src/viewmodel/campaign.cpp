#include "viewmodel/campaign.hpp"

#include <algorithm>
#include <numeric>

#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop {

namespace {

bool active(const std::optional<CampaignRun>& c) { return c && c->status == CampaignStatus::Active; }

void archive(const WebContent& c, std::vector<CampaignRun>& history, const std::optional<CampaignRun>& run) {
  if (!run || run->status == CampaignStatus::Active) return;
  if (std::ranges::any_of(history, [&](const CampaignRun& h) { return h.id == run->id; })) return;
  history.push_back(*run);
  const auto keep = static_cast<std::size_t>(std::max(1, c.campaign_history));
  if (history.size() > keep) history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(keep));
}

LegResult leg_result(const WebContent& c, const RunOutcome& out) {
  return below_campaign_minimum(c, out.equity) ? LegResult::Bankrupt : out.result;
}

// Bankruptcy ends the run; a finished mission moves on; leaving stays.
void advance(const WebContent& c, CampaignRun& run, LegResult result) {
  if (result == LegResult::Bankrupt) {
    run.status = CampaignStatus::Bankrupt;
    return;
  }
  if (result == LegResult::Left) return;
  run.index += 1;
  const auto total = static_cast<int>(campaign_order(c, run).size());
  run.status = run.index >= total ? CampaignStatus::Complete : CampaignStatus::Active;
}

std::optional<double> mean(const std::vector<double>& xs) {
  if (xs.empty()) return std::nullopt;
  return std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
}

double costs_of(const LegReport& r) {
  return fmt::num(r.fees) + fmt::num(r.spread_slippage) + std::max(0.0, -fmt::num(r.funding));
}

std::vector<RegimeResult> by_regime(const std::vector<const LegTrade*>& trades) {
  std::vector<RegimeResult> rows;
  for (const LegTrade* tr : trades) {
    const std::string regime = tr->regime.empty() ? "unlabeled" : tr->regime;
    auto it = std::ranges::find_if(rows, [&](const RegimeResult& r) { return r.regime == regime; });
    if (it == rows.end()) it = rows.insert(rows.end(), RegimeResult{regime, 0, 0, 0});
    it->trades += 1;
    it->wins += fmt::num(tr->net) > 0 ? 1 : 0;
    it->net += fmt::num(tr->net);
  }
  std::ranges::stable_sort(rows, [](const RegimeResult& a, const RegimeResult& b) { return a.trades > b.trades; });
  return rows;
}

}  // namespace

CampaignRun new_campaign(const WebContent& c, std::string id, std::int64_t now) {
  return CampaignRun{std::move(id), now, c.campaign_capital, 0, CampaignStatus::Active, {}, campaign_missions(c)};
}

const std::vector<std::string>& campaign_order(const WebContent& c, const CampaignRun& run) {
  return run.order ? *run.order : c.campaign_order_v3;
}

std::string next_campaign_mission(const WebContent& c, const std::optional<CampaignRun>& run) {
  if (!active(run)) return {};
  const Ids& missions = campaign_order(c, *run);
  return run->index >= 0 && run->index < static_cast<int>(missions.size()) ? missions[static_cast<std::size_t>(run->index)]
                                                                         : std::string();
}

Progress start_campaign(const WebContent& c, Progress p, std::string id, std::int64_t now) {
  if (active(p.campaign)) p.campaign->status = CampaignStatus::Abandoned;
  archive(c, p.campaign_history, p.campaign);
  p.campaign = new_campaign(c, std::move(id), now);
  return p;
}

Progress abandon_campaign(const WebContent& c, Progress p) {
  if (!active(p.campaign)) return p;
  p.campaign->status = CampaignStatus::Abandoned;
  archive(c, p.campaign_history, p.campaign);
  return p;
}

Progress settle_campaign_run(const WebContent& c, Progress p, const CampaignLink& link, const RunOutcome& out) {
  if (!active(p.campaign) || p.campaign->id != link.id || p.campaign->index != link.index) return p;
  CampaignRun& run = *p.campaign;
  const bool settled = std::ranges::any_of(run.legs, [&](const CampaignLeg& l) { return l.run_id == out.run_id; });
  if (settled || next_campaign_mission(c, p.campaign) != out.mission_id) return p;
  const LegResult result = leg_result(c, out);
  run.legs.push_back(CampaignLeg{out.run_id, out.mission_id, run.capital, out.equity, result, out.report});
  run.capital = out.equity;
  advance(c, run, result);
  if (result == LegResult::Finished && !contains(p.assessed, out.mission_id)) p.assessed.push_back(out.mission_id);
  archive(c, p.campaign_history, p.campaign);
  return p;
}

Progress assess_elective(const WebContent& c, Progress p, const FreeRunOutcome& out) {
  if (!out.finished || out.assisted || out.campaign || !is_elective_mission(c, out.mission_id)) return p;
  if (!contains(p.assessed, out.mission_id)) p.assessed.push_back(out.mission_id);
  return p;
}

bool below_campaign_minimum(const WebContent& c, const std::string& equity) {
  return !(fmt::num(equity) >= fmt::num(c.campaign_min_capital));
}

std::string best_completed_capital(const std::vector<CampaignRun>& history) {
  std::string best;
  for (const auto& run : history) {
    if (run.status != CampaignStatus::Complete) continue;
    if (best.empty() || fmt::num(run.capital) > fmt::num(best)) best = run.capital;
  }
  return best;
}

LegReport leg_report(const json::Value& debrief, const json::Value& totals) {
  LegReport r;
  for (const auto& tr : debrief["trades"].items()) {
    std::optional<int> r_bp;
    if (!tr["rBp"].is_null()) r_bp = static_cast<int>(tr["rBp"].as_int());
    r.trades.push_back(LegTrade{tr["net"].as_string("0"), r_bp, tr["regime"].as_string()});
  }
  r.net_pnl = debrief["netPnl"].as_string("0");
  r.spread_slippage = debrief["spreadSlippage"].as_string("0");
  r.fees = totals["feesPaid"].as_string("0");
  r.funding = totals["fundingNet"].as_string("0");
  return r;
}

CampaignReport campaign_report(const WebContent& c, const std::vector<CampaignLeg>& legs) {
  CampaignReport out;
  std::vector<const LegTrade*> trades;
  double net = 0;
  for (const auto& leg : legs) {
    if (!leg.report) continue;
    ++out.missions;
    out.costs += costs_of(*leg.report);
    net += fmt::num(leg.report->net_pnl);
    for (const auto& tr : leg.report->trades) trades.push_back(&tr);
  }
  std::vector<double> with_r, wins_r, losses_r;
  for (const LegTrade* tr : trades) {
    out.wins += fmt::num(tr->net) > 0 ? 1 : 0;
    if (!tr->r_bp) continue;
    const double r = *tr->r_bp / 10000.0;
    with_r.push_back(r);
    (r > 0 ? wins_r : losses_r).push_back(r);
  }
  out.trades = static_cast<int>(trades.size());
  if (out.trades > 0) out.win_rate = static_cast<double>(out.wins) / out.trades;
  out.avg_win_r = mean(wins_r);
  out.avg_loss_r = mean(losses_r);
  out.expectancy_r = mean(with_r);
  out.without_r = out.trades - static_cast<int>(with_r.size());
  out.gross = net + out.costs;
  if (out.gross > 0) out.cost_share = out.costs / out.gross;
  out.by_regime = by_regime(trades);
  out.too_few = out.trades < c.report_min_trades;
  return out;
}

}  // namespace tradelab::desktop
