#include "viewmodel/report_texts.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop {

namespace {

std::string str(long v) { return std::to_string(v); }

int pct_digits(int bp) {
  if (bp % 100 == 0) return 0;
  return bp % 10 == 0 ? 1 : 2;
}

std::string finding_template(const Catalog& c, const std::string& code, long count) {
  const std::string base = "finding." + code;
  if (count > 0 && c.has(base + ".safe")) return base + ".safe";
  if (count == 1 && c.has(base + ".one")) return base + ".one";
  return base;
}

std::string describe_finding(const Catalog& c, const json::Value& f, int liq_warning_bp) {
  const Locale L = c.locale();
  const std::string code = f["code"].as_string();
  const long count = static_cast<long>(f["count"].as_int());
  const std::string key = finding_template(c, code, count);
  if (!c.has(key)) return code;
  const std::string warn = liq_warning_bp >= 0 ? " (" + bp_pct(liq_warning_bp, L) + ")" : std::string();
  return c.tr(key, {{"step", str(static_cast<long>(f["step"].as_int()))},
                    {"count", str(count)},
                    {"price", fmt::fmt_price(f["price"].as_string(), L)},
                    {"requested", fmt::fmt_price(f["requested"].as_string(), L)},
                    {"amount", fmt::fmt_price(f["amount"].as_string(), L)},
                    {"risk", fmt::fmt_num(std::round(fmt::num(f["amount"].as_string())), L, 0)},
                    {"pct", bp_pct(static_cast<int>(count), L)},
                    {"warn", warn}});
}

bool held_at(const json::Value& tr, long step) {
  const long entry = static_cast<long>(tr["entryStep"].as_int());
  return entry <= step && (tr["exitStep"].is_null() || tr["exitStep"].as_int() > step);
}

ReleasePosition you_at(const json::Value& trades, long step, int after_bars) {
  bool before = false;
  bool after = false;
  for (const auto& tr : trades.items()) {
    const long entry = static_cast<long>(tr["entryStep"].as_int());
    before = before || held_at(tr, step);
    after = after || (entry > step && entry <= step + after_bars);
  }
  if (before && after) return ReleasePosition::Both;
  if (before) return ReleasePosition::Before;
  return after ? ReleasePosition::After : ReleasePosition::Flat;
}

}  // namespace

std::string tr_count(const Catalog& c, const std::string& key, long n, Params params) {
  const std::string one = key + "One";
  params.emplace_back("n", fmt::fmt_num(static_cast<double>(n), c.locale(), 0));
  return c.tr(n == 1 && c.has(one) ? one : key, params);
}

std::string bp_pct(int bp, Locale locale) { return fmt::fmt_pct(bp / 10000.0, locale, pct_digits(bp)); }

std::string objective_text(const Catalog& c, const json::Value& o) {
  const std::string key = "objective." + o["code"].as_string();
  if (!c.has(key)) return o["code"].as_string();
  return c.tr(key, {{"pct", bp_pct(static_cast<int>(o["valueBp"].as_int()), c.locale())}});
}

std::vector<std::string> objective_texts(const Catalog& c, const json::Value& rules) {
  std::vector<std::string> out;
  for (const auto& o : rules["objectives"].items()) out.push_back(objective_text(c, o));
  return out;
}

std::vector<std::string> describe_findings(const Catalog& c, const json::Value& findings, int liq_warning_bp) {
  const auto& items = findings.items();
  const bool stop_named = std::ranges::any_of(
      items, [](const json::Value& f) { return f["code"].as_string() == "stop_beyond_liq" && f["count"].as_int() > 0; });
  std::vector<std::string> out;
  for (const auto& f : items) {
    if (stop_named && f["code"].as_string() == "liquidated") {
      const Locale L = c.locale();
      out.push_back(c.tr("finding.liquidatedOnly", {{"step", str(static_cast<long>(f["step"].as_int()))},
                                                    {"price", fmt::fmt_price(f["price"].as_string(), L)},
                                                    {"amount", fmt::fmt_price(f["amount"].as_string(), L)}}));
      continue;
    }
    out.push_back(describe_finding(c, f, liq_warning_bp));
  }
  return out;
}

std::string describe_lesson(const Catalog& c, const std::string& lesson) {
  const std::string key = "nextLesson." + lesson;
  return c.has(key) ? c.tr(key) : lesson;
}

std::string fmt_leverage_bp(double bp, Locale locale) {
  if (!std::isfinite(bp)) return "\xE2\x80\x94";
  return fmt::fmt_num(bp / 10000.0, locale, 1) + "x";
}

std::string fmt_liq_distance(double bp, Locale locale) {
  return bp < 0 ? std::string("\xE2\x80\x94") : fmt::fmt_pct(bp / 10000.0, locale, 2);
}

std::string fmt_bp(double bp, Locale locale) { return fmt::fmt_pct(bp / 10000.0, locale, 2, true); }

std::string stop_advice_text(const Catalog& c, const json::Value& tr) {
  if (tr["series"].as_int() != 0 || !tr["stopBeyondLiq"].as_bool(false)) return {};
  const Locale L = c.locale();
  const long lev = static_cast<long>(tr["safeLeverage"].as_int(0));
  return c.tr(lev > 0 ? "trade.stopBeyond" : "trade.stopBeyondNone",
              {{"stop", fmt::fmt_price(tr["stop"].as_string(), L)},
               {"liq", fmt::fmt_price(tr["liqPrice"].as_string(), L)},
               {"lev", str(lev)},
               {"used", str(static_cast<long>(tr["leverage"].as_int(0)))}});
}

std::string trade_label(const Catalog& c, const json::Value& tr) {
  const bool long_side = tr["side"].as_string() == "long";
  const std::string arrow = long_side ? "\xE2\x96\xB2" : "\xE2\x96\xBC";  // ▲ ▼
  if (tr["series"].as_int() == 0) return arrow + (long_side ? " LONG" : " SHORT");
  const std::string kind = tr["kind"].as_string();
  const std::string strike = tr["strike"].as_string();
  const std::string series =
      !kind.empty() && !strike.empty()
          ? (kind == "call" ? "C " : "P ") + fmt::fmt_price(strike, c.locale()) + " \xC2\xB7 exp " +
                str(static_cast<long>(tr["expiryStep"].as_int()))
          : "OPT#" + tr["series"].as_string();
  return arrow + " " + c.tr(long_side ? "trade.optBuy" : "trade.optWrite") + " " + series;
}

std::string regime_name(const Catalog& c, const std::string& label, int direction) {
  const std::string name = c.tr("regime." + label);
  if (direction == 0) return name;
  return name + " " + c.tr(direction > 0 ? "regime.up" : "regime.down");
}

std::string fmt_tally(const Catalog& c, const json::Value& t) {
  const long samples = static_cast<long>(t["samples"].as_int());
  if (samples == 0) return "\xE2\x80\x94";
  const long hits = static_cast<long>(t["hits"].as_int());
  return c.tr("regime.tally", {{"hits", str(hits)},
                               {"n", str(samples)},
                               {"pct", fmt::fmt_pct(static_cast<double>(hits) / samples, c.locale(), 0)}});
}

std::string entry_regime_name(const Catalog& c, const json::Value& tr, const json::Value& spans) {
  const std::string regime = tr["regime"].as_string();
  if (regime.empty()) return "\xE2\x80\x94";
  const long entry = static_cast<long>(tr["entryStep"].as_int());
  for (const auto& s : spans.items()) {
    const long start = static_cast<long>(s["start"].as_int());
    if (s["label"].as_string() == regime && entry >= start && entry < start + s["bars"].as_int()) {
      return regime_name(c, regime, static_cast<int>(s["direction"].as_int()));
    }
  }
  return regime_name(c, regime);
}

std::vector<BandPiece> regime_band(const json::Value& spans, int from, int to) {
  std::vector<BandPiece> out;
  const int total = to - from + 1;
  if (total <= 0) return out;
  for (const auto& s : spans.items()) {
    const int start = static_cast<int>(s["start"].as_int());
    const int lo = std::max(start, from);
    const int hi = std::min(start + static_cast<int>(s["bars"].as_int()) - 1, to);
    if (hi < lo) continue;
    out.push_back(BandPiece{s["label"].as_string(), static_cast<int>(s["direction"].as_int()), lo, hi - lo + 1,
                            static_cast<double>(lo - from) / total, static_cast<double>(hi - lo + 1) / total});
  }
  return out;
}

std::vector<ReleaseRow> release_rows(const json::Value& events, const json::Value& trades, int last_step,
                                     int after_release_bars) {
  std::vector<ReleaseRow> out;
  for (const auto& e : events.items()) {
    const long step = static_cast<long>(e["step"].as_int());
    if (step > last_step) continue;
    const double weighted = e["surprise"].as_double() * e["credibility"].as_double() / 100.0;
    out.push_back(ReleaseRow{e, weighted, you_at(trades, step, after_release_bars)});
  }
  return out;
}

std::string_view release_position_name(ReleasePosition p) {
  switch (p) {
    case ReleasePosition::Before: return "before";
    case ReleasePosition::After: return "after";
    case ReleasePosition::Both: return "both";
    case ReleasePosition::Flat: return "flat";
  }
  return "flat";
}

std::vector<GreekRow> greek_rows(const json::Value& g) {
  if (g.is_null()) return {};
  std::vector<GreekRow> out;
  for (const char* key : {"delta", "gamma", "theta", "vega", "residual"}) out.push_back({key, fmt::num(g[key].as_string())});
  return out;
}

namespace {

ComparisonLine comparison_row(const json::Value& r) {
  ComparisonLine l;
  l.key = r["name"].as_string();
  l.name = "comparison." + l.key;
  l.refused = r["refused"].is_null() ? std::string() : r["refused"].as_string();
  l.entry_step = static_cast<int>(r["entryStep"].as_int());
  if (!l.refused.empty()) return l;
  l.net = fmt::num(r["netPnl"].as_string());
  l.drawdown = fmt::num(r["maxDrawdown"].as_string());
  l.margin_calls = static_cast<int>(r["marginCalls"].as_int());
  l.exit_step = static_cast<int>(r["exitStep"].as_int());
  return l;
}

}  // namespace

std::vector<ComparisonLine> comparison_lines(const json::Value& comparison, double player_net) {
  std::vector<ComparisonLine> lines;
  lines.push_back({"you", "comparison.you", player_net, std::nullopt, std::nullopt, std::nullopt, std::nullopt, "", false});
  for (const auto& r : comparison["rows"].items()) lines.push_back(comparison_row(r));
  std::optional<double> top;
  for (const auto& l : lines) {
    if (l.net && (!top || *l.net > *top)) top = l.net;
  }
  for (auto& l : lines) l.best = top && l.net == top;
  return lines;
}

DecisionCounts decision_counts(const json::Value& decisions) {
  DecisionCounts out;
  for (const auto& d : decisions.items()) {
    const std::string kind = d["kind"].as_string();
    if (kind == "act") ++out.act;
    if (kind == "hold") ++out.hold;
    if (kind == "wait") ++out.wait;
  }
  return out;
}

std::vector<std::string> brief_tiers(const Catalog& c, const json::Value& contract) {
  std::vector<std::string> out;
  const auto& tiers = contract["marginTiers"].items();
  const long cap = static_cast<long>(contract["leverageCap"].as_int());
  for (std::size_t i = 1; i < tiers.size(); ++i) {
    const json::Value& t = tiers[i];
    out.push_back(c.tr("brief.tier", {{"floor", fmt::fmt_num(fmt::num(t["notionalFloor"].as_string()), c.locale(), 0)},
                                      {"mmr", fmt::fmt_pct(fmt::num(t["maintenanceMargin"].as_string()), c.locale(), 1)},
                                      {"max", str(std::min(static_cast<long>(t["maxLeverage"].as_int()), cap))}}));
  }
  return out;
}

}  // namespace tradelab::desktop
