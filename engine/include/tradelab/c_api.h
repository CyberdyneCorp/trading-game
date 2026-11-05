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
/* Builds a private, masked pack from a CSV (metadata may add "leverageCap" 1..20 and "warmupBars"),
 * signed with the device key derived from a 32-byte seed (64 hex). Returns {"ok","reason","report",
 * "scenarioId","digest","size"}; the pack bytes are then read with tl_blob_data/tl_blob_size. */
const char* tl_pack_build_csv(const char* csv, const char* meta_json, const char* device_seed_hex);
const uint8_t* tl_blob_data(void);
uint32_t tl_blob_size(void);
/* {"id":"device-<16 hex>","publicKey":"<64 hex>"} for a device seed, or {"error":"invalid_seed"}. */
const char* tl_device_key(const char* device_seed_hex);
/* Verifies a pack for install (publish = 0) or publishing (publish = 1, adds LIC01 checks):
 * {"ok":bool,"reason","detail","pack":{summary without prices or reveal}}. */
const char* tl_pack_validate(const uint8_t* data, uint32_t size, const char* trusted_keys_json, int32_t publish);
/* Starts a run on a verified pack; NULL when the pack fails validation. */
tl_run* tl_run_create_pack(const uint8_t* data, uint32_t size, const char* trusted_keys_json, uint32_t seed);
tl_run* tl_run_restore_pack(const uint8_t* data, uint32_t size, const char* trusted_keys_json, uint32_t seed,
                            const char* checkpoint_hex);

#ifdef __cplusplus
}
#endif

#endif /* TRADELAB_C_API_H */
