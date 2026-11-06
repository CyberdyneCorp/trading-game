#include "viewmodel/game/panels_vm.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"
#include "viewmodel/game/options_math.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr const char* kDash = "\xE2\x80\x94";  // —
constexpr const char* kDot = " \xC2\xB7 ";     // " · "

Tone signed_tone(double v) { return v >= 0 ? Tone::Up : Tone::Down; }

std::string signed_money(double v, Locale L) { return std::string(fmt::sign_mark(v)) + " " + fmt::fmt_money(v, L, true); }

void position_block(const Texts& t, const GameState& g, RailView& r) {
  const Locale L = t.locale();
  const double qty = g.position.qty;
  r.open = qty != 0;
  if (!r.open) {
    r.position = {t.tr("rail.flat") + kDot + t.tr("rail.leverage") + " " + std::to_string(g.leverage) + "x", Tone::Muted};
    return;
  }
  const bool lng = qty > 0;
  r.position = {std::string(lng ? "\xE2\x96\xB2 " : "\xE2\x96\xBC ") + t.tr(lng ? "rail.long" : "rail.short") + " " +
                    fmt::fmt_num(std::fabs(qty), L, 1),
                lng ? Tone::Up : Tone::Down};
  const double unreal = g.account.unrealized;
  const double lev_eff = g.account.equity > 0 ? g.account.notional / g.account.equity : 0;
  r.details = {{t.tr("rail.entry"), fmt::fmt_num(g.position.entry, L, 2), Tone::Plain},
               {t.tr("rail.unrealized"), signed_money(unreal, L), signed_tone(unreal)},
               {t.tr("rail.margin"), fmt::fmt_money(g.position.margin, L), Tone::Plain},
               {t.tr("rail.notional"), fmt::fmt_money(g.account.notional, L), Tone::Plain},
               {t.tr("rail.leverage"),
                std::to_string(g.leverage) + "x (" + fmt::fmt_num(lev_eff, L, 1) + "x " + t.tr("rail.effective") + ")",
                Tone::Plain}};
  const auto liq = g.account.liquidation;
  const double mark = g.account.mark;
  const std::optional<double> dist = liq ? std::optional<double>(std::fabs(mark - *liq) / mark) : std::nullopt;
  r.liq_label = t.tr("rail.liq");
  r.liq_price = liq ? fmt::fmt_num(*liq, L, 2) : kDash;
  r.liq_distance = t.tr("rail.liqDistance") + ": " + (dist ? fmt::fmt_pct(*dist, L) : std::string(kDash));
  r.liq_danger = dist && *dist < g.rules.liq_warning_bp / 10'000.0;
  if (r.liq_danger) r.liq_warning = "\xE2\x9A\xA0 " + t.tr("rail.liqWarning", {{"pct", bp_pct(g.rules.liq_warning_bp, L)}});
}

const OptionQuote* quote_of(const Options& o, int series) {
  for (const auto& c : o.chain) {
    if (c.id == series) return &c;
  }
  return nullptr;
}

// "▲ C 100 · exp 96 × 1,0   ▲ +0,40 cr", marked at the bid (long) or the ask (short).
Line option_line(const Options& o, const OptionPosition& p, Locale L) {
  const OptionQuote* q = quote_of(o, p.series);
  const double mark_px = q == nullptr ? 0 : p.qty > 0 ? q->bid : q->ask;
  const double pnl = (mark_px - p.avg_premium) * p.qty * o.multiplier;
  const std::string name = q != nullptr ? series_label(*q, L) : "#" + std::to_string(p.series);
  return {std::string(p.qty > 0 ? "\xE2\x96\xB2 " : "\xE2\x96\xBC ") + name + " \xC3\x97 " + fmt::fmt_num(std::fabs(p.qty), L, 1) +
              "   " + signed_money(pnl, L),
          signed_tone(pnl)};
}

void options_block(const Texts& t, const GameState& g, RailView& r) {
  const Options& o = g.options;
  if (!o.enabled || o.positions.empty()) return;
  const Locale L = t.locale();
  r.options = true;
  for (const auto& p : o.positions) r.option_positions.push_back(option_line(o, p, L));
  r.option_book.push_back({t.tr("opt.value"), fmt::fmt_money(o.long_value - o.short_value, L, true), Tone::Plain});
  if (o.initial_margin > 0) {
    r.option_book.push_back({t.tr("opt.marginUsed"), fmt::fmt_money(o.initial_margin, L) + " / " + fmt::fmt_money(o.maintenance_margin, L), Tone::Plain});
  }
  const BookGreeks gk = position_greeks(o);
  r.option_book.push_back({t.tr("opt.bookGreeks"),
                           "\xCE\x94 " + fmt::fmt_num(gk.delta, L) + " \xC2\xB7 \xCE\x93 " + fmt::fmt_num(gk.gamma, L, 3) +
                               " \xC2\xB7 \xCE\x98 " + fmt::fmt_num(gk.theta, L) + " \xC2\xB7 \xCE\xBD " + fmt::fmt_num(gk.vega, L),
                           Tone::Plain});
}

}  // namespace

RailView rail_view(const Texts& t, const GameState& g, std::optional<double> planned_risk) {
  const Locale L = t.locale();
  RailView r;
  r.equity = fmt::fmt_money(g.account.equity, L);
  const double pnl = g.account.equity - g.initial_balance;
  r.pnl = {signed_money(pnl, L) + " (" + fmt::fmt_pct(pnl / g.initial_balance, L, 2, true) + ")", signed_tone(pnl)};
  r.account = {{t.tr("rail.wallet"), fmt::fmt_money(g.account.wallet, L), Tone::Plain},
               {t.tr("rail.available"), fmt::fmt_money(g.account.available, L), Tone::Plain},
               {t.tr("rail.realized"), fmt::fmt_money(g.totals.realized_pnl, L, true), Tone::Plain},
               {t.tr("rail.fees"), fmt::fmt_money(g.totals.fees_paid, L), Tone::Plain},
               {t.tr("rail.funding"), fmt::fmt_money(g.totals.funding_net, L, true), Tone::Plain}};
  position_block(t, g, r);
  if (planned_risk) r.planned_risk = KeyValue{t.tr("rail.plannedRisk"), fmt::fmt_money(*planned_risk, L), Tone::Plain};
  options_block(t, g, r);
  const double funding = g.account.next_funding_rate;
  r.funding_title = "Funding";
  r.funding = {t.tr("rail.nextFunding"), fmt::fmt_pct(funding, L, 4, true),
               funding > 0 ? Tone::Down : funding < 0 ? Tone::Up : Tone::Plain};
  r.funding_in = t.count("rail.fundingIn", g.account.next_funding_step - g.step);
  if (funding != 0) r.funding_who = t.tr("rail.fundingWho", {{"who", t.tr(funding > 0 ? "rail.longs" : "rail.shorts")}});
  for (const auto& o : g.rules.objectives) r.objectives.push_back(objective_text(t, o));
  return r;
}

std::vector<OrderRow> order_rows(const Texts& t, const GameState& g, const SeriesNames& names) {
  const Locale L = t.locale();
  std::vector<OrderRow> rows;
  for (const auto& o : g.orders) {
    const bool buy = o.side == "buy";
    std::string text = "#" + std::to_string(o.id) + "  ";
    if (o.series) text += names.name(o.series, L) + " ";
    text += std::string(order_type_label(o.type)) + "  " + (buy ? "\xE2\x96\xB2 BUY" : "\xE2\x96\xBC SELL") + "  " +
            fmt::fmt_num(o.qty, L, 1) + "  " + order_price_text(t, o);
    if (o.reduce_only) text += "  [" + t.tr("orders.reduceOnly") + "]";
    if (o.trail_rate > 0) text += "  [" + fmt::fmt_pct(o.trail_rate, L) + "]";
    if (o.parent_id) text += "  [OCO \xE2\x86\xB3#" + std::to_string(o.parent_id) + "]";
    if (o.bracket) text += "  [BRK]";
    rows.push_back({o.id, text, buy ? Tone::Up : Tone::Down});
  }
  return rows;
}

std::vector<Line> feed_lines(const Texts& t, const GameState& g, const SeriesNames& names, std::size_t max) {
  std::vector<Line> out;
  for (auto it = g.events.rbegin(); it != g.events.rend() && out.size() < max; ++it) {
    if (auto line = describe_event(t, *it, names)) {
      line->text = "[" + std::to_string(it->step) + "] " + line->text;
      out.push_back(std::move(*line));
    }
  }
  return out;
}

namespace {

Line news_line(const Texts& t, const News& n) {
  const std::string early = n.early ? "[" + t.tr("news.early") + "] " : "";
  return {"[" + std::to_string(n.step) + "] " + early + news_headline(t, n),
          n.tone > 0 ? Tone::Up : n.tone < 0 ? Tone::Down : Tone::Plain};
}

// Expected, actual and credibility; an early headline also says when it goes public.
Line news_meta(const Texts& t, const News& n, int step) {
  std::string meta = n.early ? t.count("news.publicIn", n.step - step) + kDot : "";
  meta += t.tr("news.expected") + ": " + news_level(t, n.expectation) + kDot + t.tr("news.actual") + ": " +
          news_level(t, n.actual) + kDot + t.tr("news.credibility") + " " + std::to_string(n.credibility) + "%";
  return {meta, Tone::Muted};
}

// The latest read: stance with confidence, stop and size, the reasons with their votes.
void advice_read(const Texts& t, const GameState& g, const Advice& a, AdvisorView& v) {
  const Locale L = t.locale();
  v.has_read = true;
  const Tone tone = a.stance == "long" ? Tone::Up : a.stance == "short" ? Tone::Down : Tone::Attention;
  v.stance = {t.tr("adv.stance." + a.stance) + "   " +
                  t.tr("adv.confidence", {{"pct", std::to_string(pct_from_bp(a.confidence_bp))}}) + kDot +
                  t.tr("adv.at", {{"n", std::to_string(a.step)}}),
              tone};
  const double size = fmt::num(a.size);
  if (a.stance != "wait") {
    v.detail = t.tr("adv.stop") + ": " + fmt::fmt_price(a.stop, L) + kDot + t.tr("adv.size") + ": " +
               (size > 0 ? fmt::fmt_qty(a.size, L) : std::string(kDash));
  }
  v.actionable = a.stance != "wait" && size > 0 && a.step == g.step;
  for (const auto& r : a.reasons) {
    v.reasons.push_back({vote_mark(r.vote) + " " + advice_reason_line(t, r, g.indicators.patterns),
                         r.vote > 0 ? Tone::Up : r.vote < 0 ? Tone::Down : Tone::Muted});
  }
  v.disclaimer = t.tr("adv.disclaimer");
}

}  // namespace

NewsView news_view(const Texts& t, const GameState& g) {
  NewsView v;
  for (const auto& u : g.upcoming) {
    v.upcoming.push_back("\xC2\xBB " + t.tr("news.upcomingItem", {{"kind", news_kind(t, u.kind)},
                                                                         {"step", std::to_string(u.step)},
                                                                         {"consensus", news_level(t, u.expectation)}}) +
                         kDot + t.count("news.inBars", u.step - g.step));
  }
  if (!g.upcoming.empty()) v.upcoming_note = t.tr("news.upcomingNote");
  for (const int at : g.calendar) {
    const bool announced = std::any_of(g.upcoming.begin(), g.upcoming.end(), [&](const Upcoming& u) { return u.step == at; });
    if (announced) continue;
    v.calendar.push_back("\xC2\xBB " + t.tr("news.calendarItem", {{"step", std::to_string(at)}}) + kDot +
                         t.count("news.inBars", at - g.step));
  }
  for (auto it = g.news.rbegin(); it != g.news.rend(); ++it) {
    v.items.push_back(news_line(t, *it));
    v.items.push_back(news_meta(t, *it, g.step));
  }
  if (g.news.empty()) v.items.push_back({t.tr("news.none"), Tone::Muted});
  v.note = t.tr("news.note");
  return v;
}

AdvisorView advisor_view(const Texts& t, const GameState& g, bool busy) {
  const Advisor& adv = g.advisor;
  AdvisorView v;
  v.points = t.tr("adv.left", {{"n", std::to_string(adv.left)}});
  const Advice* latest = adv.advice.empty() ? nullptr : &adv.advice.back();
  const bool asked_now = latest != nullptr && latest->step == g.step;
  v.can_ask = adv.left > 0 && !asked_now && !busy && g.active();
  if (asked_now) v.ask_tooltip = t.tr("adv.askedNow");
  if (latest == nullptr) {
    v.intro = t.tr(adv.left == 0 ? "adv.empty" : "adv.intro");
    return v;
  }
  advice_read(t, g, *latest, v);
  return v;
}

std::optional<int> branch_turn(const GameState& g) {
  if (g.market_mode != "branch") return std::nullopt;
  const auto first = std::find_if(g.bars.begin(), g.bars.end(), [](const GameBar& b) { return b.synthetic; });
  if (first == g.bars.end()) return std::nullopt;
  return static_cast<int>(first - g.bars.begin()) - g.warmup_bars + 1;
}

KeyHints key_hints(const Texts& t, const Keymap& map, bool muted) {
  const auto key = [&](Action a) { return key_label(map.key(a)); };
  const auto tip = [&](const std::string& text, Action a) { return text + " (" + key(a) + ")"; };
  KeyHints h;
  h.patterns = t.tr("patterns.open") + " [" + key(Action::Patterns) + "]";
  h.patterns_tip = key(Action::Patterns);
  h.sound_tip = tip(t.tr(muted ? "sound.unmute" : "sound.mute"), Action::Mute);
  h.help_tip = tip(t.tr("top.help"), Action::Help);
  h.journal_tip = tip(t.tr("journal.open"), Action::Journal);
  h.options_tip = tip(t.tr("opt.tabOptions"), Action::Options);
  h.buy_tip = tip(t.tr("ticket.buy"), Action::Buy);
  h.sell_tip = tip(t.tr("ticket.sell"), Action::Sell);
  return h;
}

TopBarView top_bar(const Texts& t, const GameState& g) {
  const int playable = g.last_decision_step - g.warmup_bars + 1;
  const int turn = std::max(0, g.step - g.warmup_bars + 1);
  const bool branch = branch_turn(g).has_value();
  return {t.tr("top.market") + " " + g.alias + " " + t.tr(branch ? "top.branch" : "top.synthetic"),
          t.tr("mission." + g.mission + ".title"),
          t.tr("top.step") + " " + std::to_string(turn) + " " + t.tr("top.of") + " " + std::to_string(playable) + kDot +
              interval_label(g.step_seconds)};
}

}  // namespace tradelab::desktop::game
