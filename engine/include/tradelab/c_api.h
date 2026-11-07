/* Stable C ABI used by the WASM, Python and JNI bindings.
 *
 * Every function returning `const char*` returns UTF-8 JSON owned by the run (or a static
 * buffer for run-less calls), valid until the next call on the same run. Monetary values
 * are decimal strings of fixed-point integers; never parse them into floats for state.
 * Only bars up to the current decision step are ever serialized (no lookahead).
 */
#ifndef TRADELAB_C_API_H
#define TRADELAB_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_run tl_run; /* NOLINT(readability-identifier-naming): C ABI */

/* Order types: 0 market, 1 limit, 2 stop-market, 3 take-profit, 4 trailing-stop. Sides: 0 buy, 1 sell. */

const char* tl_version(void);
/* The C ABI version (engine.hpp kAbiVersion). Builds without this function predate it (ABI 0). */
uint32_t tl_abi_version(void);
const char* tl_missions(void);

/* Returns NULL for an unknown mission id. */
tl_run* tl_run_create(const char* mission_id, uint32_t seed);
/* With start options: advice allowance, fast-wire lead in bars (0..255), calendar on/off (policy v11);
 * campaign starting balance (decimal string > 0; NULL or "" = the mission's) and leverage cap
 * (> 0 lowers the mission's cap; 0 = the mission's) (policy v12). Returns NULL for an unknown
 * mission or invalid options. tl_run_create(m, s) == tl_run_create_ex(m, s, 0, 0, 0, NULL, 0). */
tl_run* tl_run_create_ex(const char* mission_id, uint32_t seed, uint32_t advice_allowance, uint32_t news_lead,
                         int32_t calendar, const char* starting_balance, int32_t leverage_cap);
void tl_run_destroy(tl_run* run);

const char* tl_place_order(tl_run* run, uint32_t client_id, int32_t type, int32_t side, const char* qty,
                           const char* price, int32_t reduce_only);
/* Bracket entry (attach_stop/attach_tp) and trailing stops (trail_distance); NULL or "" = not set. */
const char* tl_place_order_ex(tl_run* run, uint32_t client_id, int32_t type, int32_t side, const char* qty,
                              const char* price, int32_t reduce_only, const char* attach_stop,
                              const char* attach_tp, const char* trail_distance);
/* Trailing stop whose distance is a rate of the best close (policy v16), a decimal fraction in (0, 1):
 * "0.02" trails 2% behind it. Rejected with invalid_tick when the rate is missing or out of range. */
const char* tl_place_trailing_rate(tl_run* run, uint32_t client_id, int32_t side, const char* qty,
                                   int32_t reduce_only, const char* trail_rate);
/* Option order on series `series`: type 0 market, 1 limit (premium); executes at the next bar's open. */
const char* tl_place_option_order(tl_run* run, uint32_t client_id, uint32_t series, int32_t side, int32_t type,
                                  const char* qty, const char* premium);
const char* tl_cancel_order(tl_run* run, uint32_t client_id, uint32_t order_id);
/* Moves a working reduce-only stop or take-profit to `price` in place (same id, OCO link kept; policy v13). */
const char* tl_move_order(tl_run* run, uint32_t client_id, uint32_t order_id, const char* price);
const char* tl_set_leverage(tl_run* run, uint32_t client_id, int32_t leverage);
const char* tl_close_position(tl_run* run, uint32_t client_id);

/* Adds (> 0) or removes (< 0) isolated margin on the open perpetual position (decimal string). */
const char* tl_adjust_margin(tl_run* run, uint32_t client_id, const char* amount);

/* Structured plan before an exposure-increasing order (free text never reaches the engine). */
const char* tl_record_plan(tl_run* run, uint32_t client_id, int32_t side, int32_t skipped, int32_t option,
                           int32_t has_lose_scenario, const char* invalidation, const char* target,
                           int32_t confidence_pct, const char* planned_risk);
/* Calibration answer for the question due now: probability in basis points (0..10000). */
const char* tl_answer_question(tl_run* run, uint32_t client_id, int32_t p_bp, int32_t skipped);

/* Lesson question of the mission's rubric (index `question`): `choice` for multiple choice, `value` (a
 * decimal string) for a numeric one; each question is answered (or skipped) once. Rejected with
 * unknown_question, already_answered or invalid_answer. Scored as the understanding dimension. */
const char* tl_answer_lesson(tl_run* run, uint32_t client_id, uint32_t question, uint32_t choice, const char* value,
                             int32_t skipped);
/* The mission's lesson questions without the answer key (run-less, static buffer):
 * {"mission","reviewed","questions":[{"id","kind":"choice|numeric","choices","points"}]};
 * {"error":"no_lesson"} for a mission without a rubric. Texts live in the client catalogs by id. */
const char* tl_lesson(const char* mission_id);

/* Advisor read of the visible market at the current step; uses one unit of the allowance.
 * Rejected with no_advice_left (and no state change) when the allowance is used up. */
const char* tl_request_advice(tl_run* run, uint32_t client_id);

/* Checkpoint of the full run state as {"checkpoint":"<hex>","step":n}. */
const char* tl_checkpoint(tl_run* run);
/* Rebuilds the mission scenario and restores a checkpoint; NULL if incompatible or damaged. */
tl_run* tl_run_restore(const char* mission_id, uint32_t seed, const char* checkpoint_hex);

/* Advances up to `steps` bars (stops early if the run ends) and returns the state. */
const char* tl_advance(tl_run* run, int32_t steps);
const char* tl_state(tl_run* run);
/* Available once the run is finished or bankrupt; otherwise {"error":"run_active"}. */
const char* tl_debrief(tl_run* run);

/* G4 seeded reveal of a built-in mission (run-less, static buffer): the regime spans as played
 * {"start","bars","label","direction"} and each event's {"step","kind","actual","surprise",
 * "intensityPct","reaction"}; {"error":"unknown_mission"} otherwise. It describes the future of a run
 * on that mission and seed: clients ask for it only after the run has ended (design D6). */
const char* tl_mission_reveal(const char* mission_id, uint32_t seed);

/* ---- Report-only option analysis (design D4 of add-academy-and-strategies). ----
 * None of these changes a run, its log, checkpoint or digests, and none reads data after the
 * current step. Errors: {"error":"invalid_input"} (malformed or out-of-range request) or
 * {"error":"series_not_listed"}. Output (WhatIfResult in the IDL): per leg the value per unit
 * (Price, not tick-rounded), IV used (and whether the 5% floor applied), delta and gamma per unit
 * (1e-10), theta per step and vega per volatility point (Price), the position value and P&L
 * (Money, signed: short legs negative); "net" sums them (delta and gamma at 1e-8); "expiry" (null
 * unless every option leg shares one expiry) has the exact breakevens, maxProfit and maxLoss (a
 * positive maxLoss is a loss; null = unbounded, always above, since prices stop at 0) and the
 * payoff curve; "today" is the curve at the shifted time and IV. Curves are [price, pnl] pairs on
 * the grid (default forward -30%..+30%, 61 points; at most 401). */

/* Run-less (the Academy, static buffer): {"forward","minutesPerStep","multiplier",
 * "legs":[{"kind":"call|put|perp","side":"buy|sell","qty","strike","minutesToExpiry","iv","entry"}],
 * "shift":{"forward","minutesElapsed","ivPoints"},"grid":{"lo","hi","points"}}. sigma = max(5%,
 * iv + ivPoints / 100); a perpetual leg's units are qty x multiplier like an option's. */
const char* tl_option_whatif(const char* request_json);
/* The run's valuation (the terminal lab): legs {"series"} or {"kind":"perp"}, "side", "qty" and an
 * optional "entry" (default: the ask to buy, the bid to sell, the last close for the perpetual);
 * "shift":{"forward","bars","ivPoints"}. The IV model uses the base published at the current step,
 * the term factor at the shifted time and skew and smile at the shifted forward; with no shift each
 * leg's value rounds to the series' mid and its Greeks equal the quote's. */
const char* tl_run_option_whatif(tl_run* run, const char* request_json);
/* Dry run of up to 8 market legs, in order ({"legs":[{"series"} | {"kind":"perp"}, "side", "qty"]}),
 * on a copy of the run state with the command functions: per leg accepted, reason, the quote it
 * would cross, premium, fee, what the command checks ("required") and the available balance after
 * it; totals, and the margins and available balance once the accepted legs fill at today's quotes
 * (an estimate: execution uses the next open). */
const char* tl_combo_preview(tl_run* run, const char* legs_json);

/* ---- Scenario packs and private CSV import (spec scenario-packs). ----
 * Packs are byte buffers (pointer + size). trusted_keys_json is [{"id":"...","key":"<64 hex>"}]:
 * the client's pinned publisher keys plus its own device key. Reason codes are listed in
 * engine/src/pack.cpp and engine/src/csv_import.cpp. Run-less calls return a static buffer
 * valid until the next run-less call. */

/* Validates a CSV against its metadata {"asset","pair","venue","volumeUnit":"base"|"quote",
 * "intervalSeconds","timestampUnit":"s"|"ms"}: {"ok":bool,"rows":n,"errors":[{"line","code"}],
 * "errorCount","gaps":[{"line","from","to","missing"}],"gapCount","hasFunding","firstTime","lastTime"}
 * or {"ok":false,"metaError":"<field>"}. */
const char* tl_csv_validate(const char* csv, const char* meta_json);
/* Builds a private, masked pack from a CSV (metadata may add "leverageCap" 1..20, "warmupBars" and
 * "gapFill": "none" (default), "skip" or "carryForward" to fill gaps with continuity bars, policy v17),
 * signed with the device key derived from a 32-byte seed (64 hex). Returns {"ok","reason","report",
 * "scenarioId","digest","size"}; the pack bytes are then read with tl_blob_data/tl_blob_size. */
const char* tl_pack_build_csv(const char* csv, const char* meta_json, const char* device_seed_hex);
const uint8_t* tl_blob_data(void);
uint32_t tl_blob_size(void);
/* {"id":"device-<16 hex>","publicKey":"<64 hex>"} for a device seed, or {"error":"invalid_seed"}. */
const char* tl_device_key(const char* device_seed_hex);
/* Verifies a pack for install (publish = 0) or publishing (publish = 1, adds LIC01 checks):
 * {"ok":bool,"reason","detail","pack":{summary without prices or reveal}}; the summary carries the
 * pack's schemaVersion and, for a v2 pack with mission.json, its texts [{"locale","title","briefing"}]. */
const char* tl_pack_validate(const uint8_t* data, uint32_t size, const char* trusted_keys_json, int32_t publish);
/* Starts a run on a verified pack; NULL when the pack fails validation. */
tl_run* tl_run_create_pack(const uint8_t* data, uint32_t size, const char* trusted_keys_json, uint32_t seed);
tl_run* tl_run_restore_pack(const uint8_t* data, uint32_t size, const char* trusted_keys_json, uint32_t seed,
                            const char* checkpoint_hex);

/* ---- Run files (add-run-sharing-and-journal-stats design D1, D2; run-less, static buffer). ----
 * Verifies an untrusted run file (JSON, format "tradelab-run" version 1, at most 1 MiB): strict
 * parse, schema of every member and action, the file's execution policy equal to this engine's,
 * then a replay from a fresh run (its mission with its start options, or the catalog pack in
 * pack/pack_len verified against trusted_keys_json; NULL/0/NULL for a mission) with the shared
 * action applier, accepted only when the final status, digest and eventsDigest equal "result".
 * Accepted: {"ok":true,"mission","seed","pack","status","step","digest","eventsDigest","actions",
 * "rejected","fileDigest","run":<the canonical file (RFC 8785)>,"debrief":<tl_debrief> | null}.
 * Refused: {"ok":false,"reason","path","pack","detail"}: reason too_large, malformed,
 * not_a_run_file, unsupported_version, invalid_field (path of the first bad member, e.g.
 * "actions[3].qty"), policy_mismatch (detail = the file's policy), unknown_mission,
 * invalid_options, pack_required (pack = the digest to supply), pack_invalid, pack_mismatch or
 * digest_mismatch. No other run is touched. */
const char* tl_run_file_verify(const char* file_json, const uint8_t* pack, uint32_t pack_len, const char* trusted_keys_json);

/* ---- Pack authoring (tools/packer; native only, not exported to WASM). ----
 * A pack source is an uncompressed archive of manifest.json and the raw data files, read with
 * tl_blob_data/tl_blob_size. The packer compresses the .bin files (zstd frames declaring their
 * content size) and writes the signed pack with tl_pack_write. */

/* A built-in mission generated for a seed as a synthetic pack source:
 * {"ok","reason","detail","scenarioId","files":[names]}. */
const char* tl_pack_source_mission(const char* mission_id, uint32_t seed);
/* A masked CSV window as a publishable (not private) historical pack source. meta_json is the
 * tl_pack_build_csv metadata, optionally with "branchStep", "branchMission" and "branchSeed" for a
 * branch pack (bars after branchStep come from that mission and seed, marked synthetic). Returns
 * {"ok","reason","detail","report","scenarioId","files"}. */
const char* tl_pack_source_csv(const char* csv, const char* meta_json);
/* Writes a pack from a manifest and an archive of stored files, signed with the key derived from a
 * 32-byte seed (64 hex) under the id "<key_prefix>-<16 hex>" (prefix: 1-16 of [a-z]).
 * {"ok","reason","digest","size","signingKeyId"}; the pack is then in tl_blob_data/tl_blob_size. */
const char* tl_pack_write(const char* manifest_json, const uint8_t* files_tar, uint32_t size, const char* seed_hex,
                          const char* key_prefix);
/* Signs a non-private pack again with another key (same files and manifest); result as tl_pack_write. */
const char* tl_pack_resign(const uint8_t* pack, uint32_t size, const char* seed_hex, const char* key_prefix);
/* {"id":"<prefix>-<16 hex>","publicKey":"<64 hex>"} or {"error":"invalid_seed"|"invalid_prefix"}. */
const char* tl_signing_key(const char* seed_hex, const char* key_prefix);

#ifdef __cplusplus
}
#endif

#endif /* TRADELAB_C_API_H */
