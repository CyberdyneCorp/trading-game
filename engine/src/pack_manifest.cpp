#include "pack_manifest.hpp"

#include <algorithm>

namespace tradelab::detail {
namespace {

using Kind = JsonValue::Kind;

bool id_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; }

// Typed field reader with a sticky first error, so the schema reads top to bottom.
class Fields {
 public:
  explicit Fields(ManifestError* err) : err_(err) {}

  [[nodiscard]] bool failed() const { return !err_->reason.empty(); }

  const JsonValue* get(const JsonValue& parent, std::string_view key, std::string_view path, Kind kind) {
    if (failed()) return nullptr;
    const JsonValue* v = parent.find(key);
    if (v == nullptr) {
      fail("missing_field", path);
      return nullptr;
    }
    if (!v->is(kind)) {
      fail("invalid_field", path);
      return nullptr;
    }
    return v;
  }

  void str(const JsonValue& p, std::string_view key, std::string_view path, std::string* out, std::size_t max_len = 64) {
    const JsonValue* v = get(p, key, path, Kind::String);
    if (v == nullptr) return;
    if (v->text.size() > max_len) return fail("invalid_field", path);
    *out = v->text;
  }

  void id(const JsonValue& p, std::string_view key, std::string_view path, std::string* out) {
    str(p, key, path, out);
    if (!failed() && (out->empty() || !std::all_of(out->begin(), out->end(), id_char))) fail("invalid_field", path);
  }

  void one_of(const JsonValue& p, std::string_view key, std::string_view path, std::string* out,
              std::initializer_list<std::string_view> allowed) {
    str(p, key, path, out);
    if (!failed() && std::find(allowed.begin(), allowed.end(), *out) == allowed.end()) fail("invalid_field", path);
  }

  template <typename T>
  void integer(const JsonValue& p, std::string_view key, std::string_view path, T* out, std::int64_t lo, std::int64_t hi) {
    const JsonValue* v = get(p, key, path, Kind::Int);
    if (v == nullptr) return;
    if (v->integer < lo || v->integer > hi) return fail("invalid_field", path);
    *out = static_cast<T>(v->integer);
  }

  // An optional integer: absent keeps *out.
  template <typename T>
  void opt_integer(const JsonValue& p, std::string_view key, std::string_view path, T* out, std::int64_t lo, std::int64_t hi) {
    if (p.find(key) != nullptr) integer(p, key, path, out, lo, hi);
  }

  void boolean(const JsonValue& p, std::string_view key, std::string_view path, bool* out) {
    const JsonValue* v = get(p, key, path, Kind::Bool);
    if (v != nullptr) *out = v->boolean;
  }

  // Decimal string within [lo, hi] raw units of F.
  template <typename F>
  void dec(const JsonValue& p, std::string_view key, std::string_view path, F* out, std::int64_t lo, std::int64_t hi) {
    const JsonValue* v = get(p, key, path, Kind::String);
    if (v == nullptr) return;
    const auto r = parse_decimal<F>(v->text);
    if (!r.ok() || r.value().raw < lo || r.value().raw > hi) return fail("invalid_field", path);
    *out = r.value();
  }

  void fail(std::string_view reason, std::string_view path) {
    if (failed()) return;
    err_->reason = reason;
    err_->field = path;
  }

 private:
  ManifestError* err_;
};

constexpr std::int64_t kMaxI64 = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kRateOne = Rate::kScale;
constexpr std::int64_t kVolLimit = std::int64_t{10} * Vol::kScale;  // skew, smile, term slope within +-10
constexpr std::uint32_t kMaxTableRows = 100'000;  // the PackLimits caps are checked by open_pack

// instruments.options (spec "Pack manifest": option series): the pricing and margin parameters of
// OptionSpec; the series and the IV column travel in options.bin.
void read_options(Fields& f, const JsonValue& o, Manifest* m) {
  OptionSpec& x = m->options;
  f.dec(o, "multiplier", "instruments.options.multiplier", &x.multiplier, 1, kMaxI64);
  f.dec(o, "premiumTick", "instruments.options.premiumTick", &x.premium_tick, 1, kMaxI64);
  f.dec(o, "lotSize", "instruments.options.lotSize", &x.lot_size, 1, kMaxI64);
  f.dec(o, "feeRate", "instruments.options.feeRate", &x.fee_rate, 0, kRateOne / 10);
  f.dec(o, "spread", "instruments.options.spread", &x.spread, 0, kRateOne / 2);
  f.dec(o, "skew", "instruments.options.skew", &x.skew, -kVolLimit, kVolLimit);
  f.dec(o, "smile", "instruments.options.smile", &x.smile, -kVolLimit, kVolLimit);
  f.dec(o, "termSlope", "instruments.options.termSlope", &x.term_slope, -kVolLimit, kVolLimit);
  f.integer(o, "minutesPerStep", "instruments.options.minutesPerStep", &x.minutes_per_step, 1, 7 * 1440);
  f.integer(o, "settlementBars", "instruments.options.settlementBars", &x.settlement_bars, 1, 1000);
  f.dec(o, "imBase", "instruments.options.imBase", &x.im_base, 0, kRateOne);
  f.dec(o, "imFloor", "instruments.options.imFloor", &x.im_floor, 1, kRateOne);
  f.dec(o, "mmBase", "instruments.options.mmBase", &x.mm_base, 1, kRateOne);
  f.boolean(o, "allowShort", "instruments.options.allowShort", &x.allow_short);
  m->has_options = true;
}

void read_header(Fields& f, const JsonValue& r, Manifest* m) {
  f.integer(r, "schemaVersion", "schemaVersion", &m->schema_version, 1, 1000);
  f.id(r, "scenarioId", "scenarioId", &m->scenario_id);
  f.integer(r, "packVersion", "packVersion", &m->pack_version, 1, 1'000'000);
  f.one_of(r, "mode", "mode", &m->mode, {"historical", "synthetic", "branch"});
  f.boolean(r, "private", "private", &m->private_pack);
  const JsonValue* compat = f.get(r, "engineCompatibility", "engineCompatibility", Kind::Object);
  if (compat != nullptr) {
    f.str(*compat, "min", "engineCompatibility.min", &m->engine_min, 16);
    f.str(*compat, "max", "engineCompatibility.max", &m->engine_max, 16);
  }
  f.integer(r, "executionPolicyVersion", "executionPolicyVersion", &m->execution_policy, 1, 1'000'000);
  f.integer(r, "scoringVersion", "scoringVersion", &m->scoring_version, 1, 1'000'000);
  f.str(r, "timeframe", "timeframe", &m->timeframe, 8);
  f.integer(r, "stepSeconds", "stepSeconds", &m->step_seconds, 1, std::int64_t{7} * 86'400);
  f.integer(r, "displayPrecision", "displayPrecision", &m->display_precision, 0, 8);
  f.one_of(r, "aliasPolicy", "aliasPolicy", &m->alias_policy, {"per-run"});
}

void read_instrument(Fields& f, const JsonValue& r, Manifest* m) {
  const JsonValue* inst = f.get(r, "instruments", "instruments", Kind::Object);
  const JsonValue* perp = inst == nullptr ? nullptr : f.get(*inst, "perpetual", "instruments.perpetual", Kind::Object);
  if (perp == nullptr) return;
  const JsonValue* options = inst->find("options");
  if (options != nullptr && !options->is(Kind::Object)) return f.fail("invalid_field", "instruments.options");
  if (options != nullptr) read_options(f, *options, m);
  ContractSpec& c = m->contract;
  f.dec(*perp, "multiplier", "instruments.perpetual.multiplier", &c.multiplier, 1, kMaxI64);
  f.dec(*perp, "tickSize", "instruments.perpetual.tickSize", &c.tick_size, 1, kMaxI64);
  f.dec(*perp, "lotSize", "instruments.perpetual.lotSize", &c.lot_size, 1, kMaxI64);
  f.dec(*perp, "minQty", "instruments.perpetual.minQty", &c.min_qty, 1, kMaxI64);
  f.integer(*perp, "maxLeverage", "instruments.perpetual.maxLeverage", &c.max_leverage, 1, 125);
}

void read_models(Fields& f, const JsonValue& r, Manifest* m) {
  const JsonValue* cost = f.get(r, "costModel", "costModel", Kind::Object);
  if (cost != nullptr) {
    f.dec(*cost, "makerFee", "costModel.makerFee", &m->contract.maker_fee, 0, kRateOne - 1);
    f.dec(*cost, "takerFee", "costModel.takerFee", &m->contract.taker_fee, 0, kRateOne - 1);
    f.dec(*cost, "halfSpread", "costModel.halfSpread", &m->costs.half_spread, 0, kRateOne / 10);
    f.dec(*cost, "slippage", "costModel.slippage", &m->costs.slippage, 0, kRateOne / 10);
  }
  const JsonValue* liq = f.get(r, "liquidityModel", "liquidityModel", Kind::Object);
  if (liq != nullptr) f.dec(*liq, "capacityFraction", "liquidityModel.capacityFraction", &m->costs.capacity_fraction, 1, kRateOne);
  const JsonValue* fund = f.get(r, "fundingModel", "fundingModel", Kind::Object);
  if (fund != nullptr) {
    f.integer(*fund, "intervalSteps", "fundingModel.intervalSteps", &m->contract.funding_interval, 1, 10'000);
    f.one_of(*fund, "source", "fundingModel.source", &m->funding_source, {"file", "none"});
  }
  const JsonValue* iv = f.get(r, "ivModel", "ivModel", Kind::Object);
  if (iv != nullptr) f.one_of(*iv, "kind", "ivModel.kind", &m->iv_kind, {"realized", "none", "file"});
  const JsonValue* margin = f.get(r, "marginModel", "marginModel", Kind::Object);
  if (margin != nullptr) {
    f.dec(*margin, "maintenanceMargin", "marginModel.maintenanceMargin", &m->contract.maintenance_margin, 1, kRateOne - 1);
  }
}

// A branch pack names its branch point; the other modes have none.
void read_branch(Fields& f, const JsonValue& data, Manifest* m) {
  if (f.failed()) return;
  const bool branch = m->mode == "branch";
  if (branch != (data.find("branchStep") != nullptr)) return f.fail(branch ? "missing_field" : "invalid_field", "data.branchStep");
  if (!branch) return;
  std::uint32_t step = 0;
  f.integer(data, "branchStep", "data.branchStep", &step, 0, 1'000'000);
  m->branch_step = step;
}

void read_mission(Fields& f, const JsonValue& r, Manifest* m) {
  const JsonValue* rules = f.get(r, "missionRules", "missionRules", Kind::Object);
  if (rules != nullptr) {
    MissionRules& mr = m->mission;
    f.dec(*rules, "initialBalance", "missionRules.initialBalance", &mr.initial_balance, 1, kMaxI64);
    f.integer(*rules, "leverageCap", "missionRules.leverageCap", &mr.leverage_cap, 1, 125);
    f.integer(*rules, "warmupBars", "missionRules.warmupBars", &mr.warmup_bars, 0, 100'000);
    f.boolean(*rules, "allowShort", "missionRules.allowShort", &mr.allow_short);
  }
  const JsonValue* data = f.get(r, "data", "data", Kind::Object);
  if (data != nullptr) {
    f.integer(*data, "bars", "data.bars", &m->bars, 3, 1'000'000);
    f.integer(*data, "gaps", "data.gaps", &m->gaps, 0, 1'000'000);
    f.opt_integer(*data, "events", "data.events", &m->events, 0, kMaxTableRows);
    f.opt_integer(*data, "liquidityShocks", "data.liquidityShocks", &m->liquidity_shocks, 0, kMaxTableRows);
    f.opt_integer(*data, "optionSeries", "data.optionSeries", &m->option_series, 0, kMaxTableRows);
    read_branch(f, *data, m);
  }
}

bool hex64(const std::string& s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

void read_files(Fields& f, const JsonValue& r, Manifest* m) {
  const JsonValue* files = f.get(r, "files", "files", Kind::Array);
  if (files == nullptr) return;
  for (const JsonValue& e : files->items) {
    ManifestFile mf;
    if (!e.is(Kind::Object)) return f.fail("invalid_field", "files");
    f.str(e, "name", "files.name", &mf.name);
    f.integer(e, "size", "files.size", &mf.size, 0, kMaxI64);
    f.integer(e, "decompressedSize", "files.decompressedSize", &mf.decompressed_size, 0, kMaxI64);
    f.str(e, "sha256", "files.sha256", &mf.sha256);
    if (!f.failed() && !hex64(mf.sha256)) f.fail("invalid_field", "files.sha256");
    m->files.push_back(mf);
  }
}

void read_rights(Fields& f, const JsonValue& r, Manifest* m) {
  f.str(r, "signingKeyId", "signingKeyId", &m->signing_key_id);
  f.get(r, "provenance", "provenance", Kind::Object);
  const JsonValue* lic = f.get(r, "license", "license", Kind::Object);
  if (lic != nullptr) {
    ManifestLicense& l = m->license;
    f.str(*lic, "name", "license.name", &l.name);
    f.str(*lic, "attribution", "license.attribution", &l.attribution, 256);
    f.boolean(*lic, "transformation", "license.transformation", &l.transformation);
    f.boolean(*lic, "commercial", "license.commercial", &l.commercial);
    f.boolean(*lic, "offlineRedistribution", "license.offlineRedistribution", &l.offline_redistribution);
  }
  const JsonValue* dist = f.get(r, "distribution", "distribution", Kind::Object);
  if (dist != nullptr) f.boolean(*dist, "downloadable", "distribution.downloadable", &m->downloadable);
}

}  // namespace

bool parse_manifest(const JsonValue& root, Manifest* out, ManifestError* err) {
  *err = {};
  Fields f(err);
  if (!root.is(Kind::Object)) {
    f.fail("invalid_field", "manifest");
    return false;
  }
  Manifest m;
  read_header(f, root, &m);
  read_instrument(f, root, &m);
  read_models(f, root, &m);
  read_mission(f, root, &m);
  read_files(f, root, &m);
  read_rights(f, root, &m);
  if (f.failed()) return false;
  *out = std::move(m);
  return true;
}

bool parse_semver(const std::string& v, std::array<std::uint32_t, 3>* out) {
  std::size_t part = 0;
  std::uint64_t cur = 0;
  bool digits = false;
  for (const char c : v) {
    if (c == '.' && digits && part < 2) {
      (*out)[part++] = static_cast<std::uint32_t>(cur);
      cur = 0;
      digits = false;
    } else if (c >= '0' && c <= '9' && cur < 100'000) {
      cur = cur * 10 + static_cast<std::uint64_t>(c - '0');
      digits = true;
    } else {
      return false;
    }
  }
  if (!digits || part != 2) return false;
  (*out)[2] = static_cast<std::uint32_t>(cur);
  return true;
}

}  // namespace tradelab::detail
