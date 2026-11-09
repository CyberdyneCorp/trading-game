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

// analysis.ts variantOf (scoring v14): an unplanned revenge entry, an averaging add without a stop.
std::string variant_key(const json::Value& f) {
  const json::Value& e = f["evidence"];
  if (e.kind() != json::Value::Kind::Object) return {};
  const std::string code = f["code"].as_string();
  if (code == "revenge_trade" && !e["planned"].as_bool(false)) return "findingVariant.revenge_trade.unplanned";
  if (code == "averaging_down" && !(fmt::num(e["stop"].as_string("0")) > 0)) return "findingVariant.averaging_down.nostop";
  return {};
}

std::string finding_template(const Catalog& c, const json::Value& f, long count) {
  const std::string base = "finding." + f["code"].as_string();
  if (count > 0 && c.has(base + ".safe")) return base + ".safe";
  if (const std::string variant = variant_key(f); !variant.empty() && c.has(variant)) return variant;
  if (count == 1 && c.has(base + ".one")) return base + ".one";
  return base;
}

// A JSON number as JavaScript's String(n) for the integer fields of an evidence; "" when absent.
std::string int_text(const json::Value& v) { return v.kind() == json::Value::Kind::Number ? str(static_cast<long>(v.as_int())) : std::string(); }

std::string price_text(const json::Value& v, Locale L) { return v.is_null() ? std::string() : fmt::fmt_price(v.as_string(), L); }

std::string bp_text(const json::Value& v, Locale L) {
  return v.kind() == json::Value::Kind::Number ? bp_pct(static_cast<int>(v.as_int()), L) : std::string();
}

std::string scaled_text(const json::Value& v, double unit, Locale L) {
  return v.kind() == json::Value::Kind::Number ? fmt::fmt_num(v.as_double() / unit, L, 2) : std::string();
}

// analysis.ts evidenceParams: the evidence of a scoring v14 finding as text parameters.
void evidence_params(const Catalog& c, const json::Value& f, Params& p) {
  const json::Value& e = f["evidence"];
  if (e.kind() != json::Value::Kind::Object) return;
  const Locale L = c.locale();
  const bool stop_far = !e["stop"].is_null() && fmt::num(e["stop"].as_string()) > 0;
  const std::string limit = e["limit"].as_string();
  const json::Value& entry = e["entryStep"].is_null() ? e["addStep"] : e["entryStep"];
  const json::Value& loss = e["loss"].is_null() ? e["unrealized"] : e["loss"];
  p.insert(p.end(), {{"trade", int_text(e["trade"])},
                     {"exit", int_text(e["exitStep"])},
                     {"entry", int_text(entry)},
                     {"loss", price_text(loss, L)},
                     {"ratio", scaled_text(e["sizeRatioBp"], 10'000, L)},
                     {"ext", scaled_text(e["extensionAtr"], 100, L)},
                     {"closes", int_text(e["closes"])},
                     {"stopText", c.tr(stop_far ? "findingVariant.stop.far" : "findingVariant.stop.none", {{"stop", price_text(e["stop"], L)}})},
                     {"avgBefore", price_text(e["avgBefore"], L)},
                     {"avgAfter", price_text(e["avgAfter"], L)},
                     {"riskBefore", price_text(e["riskBefore"], L)},
                     {"riskAfter", price_text(e["riskAfter"], L)},
                     {"planRisk", price_text(e["planRisk"], L)},
                     {"limit", limit.empty() ? std::string() : c.tr("findingLimit." + limit)},
                     {"limitPct", bp_text(e["limitBp"], L)},
                     {"worstPct", bp_text(e["worstBp"], L)}});
}

std::string describe_finding(const Catalog& c, const json::Value& f, int liq_warning_bp) {
  const Locale L = c.locale();
  const long count = static_cast<long>(f["count"].as_int());
  const std::string key = finding_template(c, f, count);
  if (!c.has(key)) return f["code"].as_string();
  const std::string warn = liq_warning_bp >= 0 ? " (" + bp_pct(liq_warning_bp, L) + ")" : std::string();
  Params params{{"step", str(static_cast<long>(f["step"].as_int()))},
                {"count", str(count)},
                {"price", fmt::fmt_price(f["price"].as_string(), L)},
                {"requested", fmt::fmt_price(f["requested"].as_string(), L)},
                {"amount", fmt::fmt_price(f["amount"].as_string(), L)},
                {"risk", fmt::fmt_num(std::round(fmt::num(f["amount"].as_string())), L, 0)},
                {"pct", bp_pct(static_cast<int>(count), L)},
                {"warn", warn}};
  evidence_params(c, f, params);
  return c.tr(key, params);
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

namespace {

constexpr std::string_view kDot = " \xC2\xB7 ";  // " · "

// `sign` -1 reads the engine's adverse excursion (a size) as a loss in R.
std::string r_text(const json::Value& bp, Locale L, double sign = 1) {
  return bp.is_null() ? "\xE2\x80\x94" : fmt::fmt_num(0 + sign * bp.as_double() / 10'000, L, 2, true) + "R";
}

std::string fills_text(const json::Value& fills, Locale L) {
  std::string out;
  for (const json::Value& f : fills.items()) {
    if (!out.empty()) out += kDot;
    out += fmt::fmt_qty(f["qty"].as_string(), L) + " @ " + fmt::fmt_price(f["price"].as_string(), L) + " (" + f["step"].as_string() + ")";
  }
  return out;
}

std::string stops_text(const Catalog& c, const json::Value& e) {
  std::string out;
  for (const json::Value& m : e["stopMoves"].items()) {
    if (!out.empty()) out += kDot;
    out += c.tr("episodes.move." + m["kind"].as_string()) + " " + fmt::fmt_price(m["price"].as_string(), c.locale()) + " (" +
           m["step"].as_string() + ")";
  }
  if (e["trailing"].as_bool(false)) out += (out.empty() ? "" : std::string(kDot)) + "[" + c.tr("episodes.trailing") + "]";
  return out.empty() ? "\xE2\x80\x94" : out;
}

}  // namespace

std::vector<std::string> episode_cells(const Catalog& c, const json::Value& e) {
  const Locale L = c.locale();
  const bool long_side = e["side"].as_string() == "long";
  std::string entries = fills_text(e["entries"], L);
  if (e["entries"].items().size() > 1) entries += " [" + c.tr("episodes.scaled") + "]";
  const std::string exits = e["exits"].items().empty() ? c.tr("episodes.open") : fills_text(e["exits"], L);
  return {e["n"].as_string(),
          long_side ? "\xE2\x96\xB2 LONG" : "\xE2\x96\xBC SHORT",
          std::move(entries),
          exits,
          stops_text(c, e),
          r_text(e["rBp"], L) + std::string(kDot) + fmt::fmt_money(e["net"].as_string(), L, true),
          r_text(e["mfeRBp"], L) + std::string(kDot) + r_text(e["maeRBp"], L, -1)};
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

std::string comparison_basis_key(const json::Value& comparison) {
  const std::int64_t at = comparison["entryStep"].as_int();
  const auto& rows = comparison["rows"].items();
  const bool own = std::any_of(rows.begin(), rows.end(), [&](const json::Value& r) { return r["refused"].is_null() && r["entryStep"].as_int() != at; });
  if (own) return "comparison.basisOwn";
  return comparison["basis"].as_string() == "player" ? "comparison.basisPlayer" : "comparison.basisReference";
}

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
