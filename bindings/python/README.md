# tradelab-engine (Python)

ctypes binding over the engine's C ABI (`engine/include/tradelab/c_api.h`), used by the game server to re-simulate runs and compute the authoritative score.

```bash
cmake --preset native-release -S engine && cmake --build engine/build/native-release --target tradelab_capi
cd bindings/python && uv run --python 3.12 --with pytest pytest -q
```

The library is looked up in `TRADELAB_LIB`, then next to the package, then in `engine/build/native-{release,debug}`.

`Run(mission_id, seed, RunOptions(advice_allowance, news_lead, calendar))` starts a run with start options (policy v11; the defaults are a plain run), and `Run.request_advice(cid)` asks the advisor. `replay(mission_id, seed, actions, options=None)` consumes the action log the web client stores (format documented in `tradelab_engine/replay.py`). Client ids are assigned exactly as the client assigns them, so a replay is bit-identical to the client's run. `tests/test_engine.py` checks this against the native C++ golden digests.
