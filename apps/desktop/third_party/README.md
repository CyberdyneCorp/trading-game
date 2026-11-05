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
