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
const char* tl_set_leverage(tl_run* run, uint32_t client_id, int32_t leverage);
const char* tl_close_position(tl_run* run, uint32_t client_id);

/* Adds (> 0) or removes (< 0) isolated margin on the open perpetual position (decimal string). */
const char* tl_adjust_margin(tl_run* run, uint32_t client_id, const char* amount);

/* Checkpoint of the full run state as {"checkpoint":"<hex>","step":n}. */
const char* tl_checkpoint(tl_run* run);
/* Rebuilds the mission scenario and restores a checkpoint; NULL if incompatible or damaged. */
tl_run* tl_run_restore(const char* mission_id, uint32_t seed, const char* checkpoint_hex);

/* Advances up to `steps` bars (stops early if the run ends) and returns the state. */
const char* tl_advance(tl_run* run, int32_t steps);
const char* tl_state(tl_run* run);
/* Available once the run is finished or bankrupt; otherwise {"error":"run_active"}. */
const char* tl_debrief(tl_run* run);

#ifdef __cplusplus
}
#endif

#endif /* TRADELAB_C_API_H */
