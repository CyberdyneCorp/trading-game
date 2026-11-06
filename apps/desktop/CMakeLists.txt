cmake_minimum_required(VERSION 3.28)
project(tradelab_desktop VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# First thing: refuse global floating-point flags before anything is fetched or configured.
include(FloatFlagsGuard)
tradelab_check_global_float_flags()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(TRADELAB_DESKTOP_BUILD_TESTS "Build the desktop tests" ON)

# The engine, as its own subproject with its PRIVATE determinism flags and without its tests.
set(TRADELAB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../engine ${CMAKE_BINARY_DIR}/engine)

include(Dependencies)
include(EmbedResources)

set(TRADELAB_DESKTOP_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
# The web helpers' ports must compute what JavaScript computes: no fused multiply-add (Clang contracts
# `a * b - c` by default on arm64, which turned a web "no liquidation" into 6e-15). Parity fixtures
# (tests/fixtures/web) catch any difference.
set(TRADELAB_DESKTOP_FLOAT_FLAGS -ffp-contract=off)

# Resources embedded in the binary (views, generated catalogs).
set(TRADELAB_DESKTOP_RESOURCES
  views/menu.xml views/briefing.xml views/game.xml views/debrief.xml
  i18n/strings.csv packs/pinned_keys.json)
# Shell screens, retro look and the web's screen data (README "Retro look").
list(APPEND TRADELAB_DESKTOP_RESOURCES
  views/curriculum.xml views/journal.xml views/lesson.xml views/settings.xml
  theme/tradelab.theme.xml theme/tradelab-cb.theme.xml
  fonts/VT323-Regular.ttf fonts/IBMPlexMono-Regular.ttf shaders/crt.spv
  web/content.json)
tradelab_embed_resources(${CMAKE_BINARY_DIR}/generated/resources.cpp ${CMAKE_CURRENT_SOURCE_DIR}/resources
  ${TRADELAB_DESKTOP_RESOURCES})

# Trading terminal: viewmodels (src/viewmodel/game) and Raven glue (src/view/game).
set(TRADELAB_DESKTOP_GAME_VIEWMODEL_SOURCES
  src/viewmodel/game/chart_input.cpp
  src/viewmodel/game/chart_scale.cpp
  src/viewmodel/game/chart_vm.cpp
  src/viewmodel/game/dialogs_vm.cpp
  src/viewmodel/game/game_keys.cpp
  src/viewmodel/game/game_viewmodel.cpp
  src/viewmodel/game/hotkeys.cpp
  src/viewmodel/game/options_math.cpp
  src/viewmodel/game/options_vm.cpp
  src/viewmodel/game/panels_vm.cpp
  src/viewmodel/game/progress.cpp
  src/viewmodel/game/shortcuts.cpp
  src/viewmodel/game/sizing.cpp
  src/viewmodel/game/texts.cpp
  src/viewmodel/game/ticket_vm.cpp)
set(TRADELAB_DESKTOP_GAME_VIEW_SOURCES
  src/view/game/game_view.cpp
  src/view/game/game_view_dialogs.cpp
  src/view/game/game_view_panels.cpp
  src/view/game/game_view_ticket.cpp
  src/view/game/scene_painter.cpp
  src/view/game/widgets.cpp)

# ---- Layers (design "Library split enforces the layers"): model and viewmodel never see Raven. ----

add_library(tradelab_desktop_model STATIC
  src/model/engine_host.cpp
  src/model/engine_run.cpp
  src/model/game_command.cpp
  src/model/game_prefs.cpp
  src/model/game_state.cpp
  src/model/i18n.cpp
  src/model/json.cpp
  src/model/content.cpp
  ${CMAKE_BINARY_DIR}/generated/resources.cpp)
target_include_directories(tradelab_desktop_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(tradelab_desktop_model PUBLIC tradelab::engine Threads::Threads)
target_compile_options(tradelab_desktop_model PRIVATE ${TRADELAB_DESKTOP_WARNINGS} ${TRADELAB_DESKTOP_FLOAT_FLAGS})

# ---- Storage and packs (src/model/storage, src/model/packs), part of the model layer. ----
# Vendored SQLite (third_party/README.md): its own warnings off, no floating-point flags.
add_library(tradelab_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(tradelab_sqlite SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(tradelab_sqlite PRIVATE SQLITE_DQS=0 SQLITE_THREADSAFE=1 SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED)
target_compile_options(tradelab_sqlite PRIVATE -w)
target_link_libraries(tradelab_sqlite PUBLIC Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(tradelab_sqlite PUBLIC m)
endif()
target_sources(tradelab_desktop_model PRIVATE
  src/model/storage/actions.cpp
  src/model/storage/fault.cpp
  src/model/storage/json_writer.cpp
  src/model/storage/paths.cpp
  src/model/storage/progress.cpp
  src/model/storage/run_recorder.cpp
  src/model/storage/run_store.cpp
  src/model/storage/schema.cpp
  src/model/storage/sqlite.cpp
  src/model/packs/catalog.cpp
  src/model/packs/download.cpp
  src/model/packs/files.cpp
  src/model/packs/http_curl.cpp
  src/model/packs/pack_library.cpp
  src/model/update/manifest.cpp
  src/model/update/updater.cpp
  src/model/update/update_service.cpp)
# libcurl is opened at run time (dlopen), never linked.
target_link_libraries(tradelab_desktop_model PRIVATE tradelab_sqlite ${CMAKE_DL_LIBS})

add_library(tradelab_desktop_viewmodel STATIC
  src/viewmodel/app_viewmodel.cpp
  src/viewmodel/chart_geometry.cpp
  src/viewmodel/format.cpp
  src/viewmodel/screen_texts.cpp
  src/viewmodel/vulkan_message.cpp
  src/viewmodel/campaign.cpp
  src/viewmodel/curriculum.cpp
  src/viewmodel/journal.cpp
  src/viewmodel/keymap.cpp
  src/viewmodel/lesson_runner.cpp
  src/viewmodel/report_texts.cpp
  src/viewmodel/shell.cpp
  src/viewmodel/screens/screen_model.cpp
  src/viewmodel/screens/menu.cpp
  src/viewmodel/screens/briefing.cpp
  src/viewmodel/screens/debrief.cpp
  src/viewmodel/screens/debrief_tables.cpp
  src/viewmodel/screens/curriculum.cpp
  src/viewmodel/screens/journal.cpp
  src/viewmodel/screens/lesson.cpp
  src/viewmodel/screens/settings.cpp
  src/viewmodel/updates.cpp
  src/viewmodel/audio/audio_director.cpp
  src/viewmodel/audio/audio_rules.cpp
  ${TRADELAB_DESKTOP_GAME_VIEWMODEL_SOURCES})
target_link_libraries(tradelab_desktop_viewmodel PUBLIC tradelab_desktop_model)
target_compile_options(tradelab_desktop_viewmodel PRIVATE ${TRADELAB_DESKTOP_WARNINGS} ${TRADELAB_DESKTOP_FLOAT_FLAGS})

add_library(tradelab_desktop_platform STATIC src/platform/platform.cpp)
target_include_directories(tradelab_desktop_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tradelab_desktop_platform PRIVATE ${TRADELAB_DESKTOP_WARNINGS})
if(APPLE)
  find_library(TRADELAB_COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(tradelab_desktop_platform PUBLIC ${TRADELAB_COREFOUNDATION})
endif()

# The only target that links Raven; Raven calls stay in src/view (raw C API in src/view/raven).
add_library(tradelab_desktop_view STATIC
  src/view/app.cpp
  src/view/app_view.cpp
  src/view/chart_painter.cpp
  src/view/screens/retro_look.cpp
  src/view/screens/shell_view.cpp
  src/view/raven/raven_ui.cpp
  ${TRADELAB_DESKTOP_GAME_VIEW_SOURCES})
target_link_libraries(tradelab_desktop_view PUBLIC tradelab_desktop_viewmodel tradelab_desktop_platform raven::raven)
target_compile_options(tradelab_desktop_view PRIVATE ${TRADELAB_DESKTOP_WARNINGS} ${TRADELAB_DESKTOP_FLOAT_FLAGS})

# Updates (src/model/update): the release public key (64 hex) is set by the release build only; without it
# the app never checks. The key's custody and the signing of manifests are human tasks (tasks.md 1.21).
set(TRADELAB_UPDATE_PUBLIC_KEY "" CACHE STRING "Ed25519 public key (hex) that signs desktop update manifests")
set(TRADELAB_UPDATE_MANIFEST_URL
  "https://github.com/CyberdyneCorp/trading-game/releases/latest/download/desktop-manifest.json"
  CACHE STRING "Signed desktop update manifest")
target_compile_definitions(tradelab_desktop_view PRIVATE
  TRADELAB_UPDATE_PUBLIC_KEY="${TRADELAB_UPDATE_PUBLIC_KEY}"
  TRADELAB_UPDATE_MANIFEST_URL="${TRADELAB_UPDATE_MANIFEST_URL}"
  TRADELAB_DESKTOP_VERSION="${PROJECT_VERSION}")

add_executable(tradelab_desktop src/main.cpp)
target_link_libraries(tradelab_desktop PRIVATE tradelab_desktop_view)
target_compile_options(tradelab_desktop PRIVATE ${TRADELAB_DESKTOP_WARNINGS})
set_target_properties(tradelab_desktop PROPERTIES OUTPUT_NAME tradelab)

if(TRADELAB_DESKTOP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Last: no directory-wide option added above may relax floating-point semantics either.
tradelab_check_directory_float_flags(${CMAKE_CURRENT_SOURCE_DIR})
tradelab_check_global_float_flags()
