# tradelab-engine (Python)

ctypes binding over the engine's C ABI (`engine/include/tradelab/c_api.h`), used by the game server to re-simulate runs and compute the authoritative score.

```bash
cmake --preset native-release -S engine && cmake --build engine/build/native-release --target tradelab_capi
cd bindings/python && uv run --python 3.12 --with pytest pytest -q
```

The library is looked up in `TRADELAB_LIB`, then next to the package, then in `engine/build/native-{release,debug}`.

`Run(mission_id, seed, RunOptions(advice_allowance, news_lead, calendar, starting_balance, leverage_cap))` starts a run with start options (policy v11; v12 adds the campaign capital and leverage cap; the defaults are a plain run), and `Run.request_advice(cid)` asks the advisor; `Run.move_order(cid, order_id, price)` moves a working stop or take-profit in place (policy v13); `Run.place_trailing_rate(cid, side, qty, reduce_only, rate)` places a trailing stop at a rate of the best close (`"0.02"` = 2%; policy v16). `replay(mission_id, seed, actions, options=None)` consumes the action log the web client stores (format documented in `tradelab_engine/replay.py`). Client ids are assigned exactly as the client assigns them, so a replay is bit-identical to the client's run. `replay_on(run, actions)` applies a log to a run you created yourself (for example `Run.from_pack`). `tests/test_golden.py` replays the shared golden fixtures (`engine/tests/golden`, format in `engine/README.md`) and checks the same digests as the native C++ tests; `python3 tools/regen_golden.py` checks or regenerates those fixtures.
