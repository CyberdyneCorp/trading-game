// Localized terminal texts built from the web's catalogs (keys generated from i18n.svelte.ts, news.ts,
// candlePatterns.ts, objectives.ts, tutorials.ts and lessons.ts): rejection reasons with their numbers
// (rejections.ts), event feed lines (EventFeed.svelte), order labels (eventNames.ts), news (news.ts),
// advisor reasons (advisor.ts), objectives (objectives.ts).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "model/game_state.hpp"
#include "model/i18n.hpp"

namespace tradelab::desktop::game {

// Colour of a text, as the web's CSS classes (ok, error, warn, info, muted, up, down, attention).
enum class Tone : unsigned char { Plain, Ok, Error, Warn, Info, Muted, Up, Down, Attention };

struct Line {
  std::string text;
  Tone tone = Tone::Plain;
};

class Texts {
 public:
  explicit Texts(const Catalog& catalog) : catalog_(&catalog) {}
  [[nodiscard]] Locale locale() const { return catalog_->locale(); }
  [[nodiscard]] std::string tr(std::string_view key, const Params& params = {}) const { return catalog_->tr(key, params); }
  [[nodiscard]] bool has(std::string_view key) const { return catalog_->has(key); }
  // tCount: the "<key>One" text for n == 1 when the catalog has it; {n} is n in the locale.
  [[nodiscard]] std::string count(std::string_view key, long n, Params params = {}) const;
  // reasonText: "reason.<code>" or the code itself.
  [[nodiscard]] std::string reason(std::string_view code) const;
  // A catalog text when the key exists, else the fallback.
  [[nodiscard]] std::string tr_or(std::string_view key, std::string_view fallback, const Params& params = {}) const;

 private:
  const Catalog* catalog_;
};

// What the client knew when a command was refused (rejections.ts RejectionFacts); all optional.
struct RejectionFacts {
  std::optional<double> need, available, margin, initial;
  std::optional<int> leverage, cap;
};

std::string rejection_text(const Texts& t, std::string_view reason, const RejectionFacts& f = {});
RejectionFacts order_facts(const GameState& g, double need, int leverage);
RejectionFacts relever_facts(const GameState& g, int leverage);
RejectionFacts margin_facts(const GameState& g, double delta);

// MKT, LMT, STOP, TP, TRAIL.
std::string_view order_type_label(std::string_view type);
// "@open", a price, or "prêmio 1,23" for an option order.
std::string order_price_text(const Texts& t, const Order& o);

// Option series names seen during the run (the state only lists live series).
class SeriesNames {
 public:
  void reset();
  void observe(const GameState& s);
  [[nodiscard]] int series_of_order(int order_id) const;
  [[nodiscard]] std::string name(int series, Locale locale) const;

 private:
  struct Key {
    std::string kind;
    double strike = 0;
    int expiry = 0;
  };
  std::map<int, Key> series_;
  std::map<int, int> order_series_;
};

// One line of the event feed, or nullopt for an event the feed does not show.
std::optional<Line> describe_event(const Texts& t, const EngineEvent& e, const SeriesNames& names);

std::string news_headline(const Texts& t, const News& n);
std::string news_kind(const Texts& t, std::string_view kind);
std::string news_level(const Texts& t, int value);

// advisor.ts voteMark / reasonLine / pctFromBp.
std::string vote_mark(int vote);
std::string advice_reason_line(const Texts& t, const AdviceReason& r, const std::vector<PatternHit>& patterns);
int pct_from_bp(int bp);

std::string pattern_name(const Texts& t, std::string_view id);

// objectives.ts bpPct / objectiveText.
std::string bp_pct(int bp, Locale locale);
std::string objective_text(const Texts& t, const Objective& o);

// packs.ts intervalLabel: 3600 -> "1h".
std::string interval_label(int seconds);

}  // namespace tradelab::desktop::game
