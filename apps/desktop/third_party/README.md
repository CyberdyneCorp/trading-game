# Third-party code vendored in the desktop client

Fetched code (Raven, Vulkan-Headers, doctest) is pinned in `cmake/Dependencies.cmake`; this folder holds what is copied
into the repository.

## SQLite (`sqlite/`)

| | |
|---|---|
| Version | 3.54.1 (`SQLITE_SOURCE_ID` `2026-10-16 14:45:20 555e07ef33f69a0957d24d8e555b702464a131d6f1607f4c3c1e4551e8e6ca76`) |
| Source | https://sqlite.org/2026/sqlite-amalgamation-3540100.zip |
| SHA3-256 of the zip (as listed on sqlite.org/download.html) | `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30` |
| SHA-256 of the zip | `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9` |
| SHA3-256 of `sqlite3.c` | `e2b869d3003b33c942b7dd7063604fbc53553e1b66609570df39b9b7b558ee81` |
| SHA3-256 of `sqlite3.h` | `15ef3f5dbc4a7a980f828fa0d1e473cee5a124e7bb77e9bc70fc0bfa3f9a2af8` |
| License | public domain (`sqlite/LICENSE.md`) |
| Files kept | `sqlite3.c`, `sqlite3.h` (the zip's `shell.c` and `sqlite3ext.h` are not needed) |

Built as the static library `tradelab_sqlite` (`CMakeLists.txt`) with `SQLITE_DQS=0`, `SQLITE_THREADSAFE=1`,
`SQLITE_DEFAULT_FOREIGN_KEYS=1`, `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_OMIT_DEPRECATED`, its own warnings silenced and no
floating-point flags (the float-flags guard covers it like every other target). Only `src/model/storage/sqlite.cpp`
includes `sqlite3.h`.

Updating: download the new amalgamation zip, compare its SHA3-256 with the one on sqlite.org/download.html, copy
`sqlite3.c` and `sqlite3.h`, update this table (`openssl dgst -sha3-256 sqlite3.c sqlite3.h`) and run the storage tests
(`tradelab_desktop_unit_tests -ts='storage*,packs*'`) on Linux and macOS.

## miniaudio (`miniaudio/`)

| | |
|---|---|
| Version | 0.11.22 (`MA_VERSION_MAJOR.MINOR.REVISION` in the header) |
| Source | https://raw.githubusercontent.com/mackron/miniaudio/0.11.22/miniaudio.h (tag `0.11.22`) |
| SHA-256 of `miniaudio.h` | `9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410` |
| License | public domain (Unlicense) or MIT No Attribution, at the user's choice (`miniaudio/LICENSE`, copied from the end of the header) |
| Files kept | `miniaudio.h`; `miniaudio.c` is ours (the one implementation unit) |

Built as the static library `tradelab_miniaudio` with `MA_NO_ENCODING` and `MA_NO_GENERATION` and its own warnings
silenced; MP3 decoding is miniaudio's built-in dr_mp3. Backends are opened at run time: Core Audio on macOS; PulseAudio,
ALSA or JACK on Linux (nothing is linked, so a machine without them runs silent). Only `src/platform/audio_out.cpp`
includes `miniaudio.h`.

Updating: download `miniaudio.h` of the new tag, check its SHA-256 here, copy the license block from the end of the
header into `LICENSE`, and run the audio tests (`tradelab_desktop_unit_tests -ts='*audio*'` and the headless binary's
null-device test) on Linux and macOS.
