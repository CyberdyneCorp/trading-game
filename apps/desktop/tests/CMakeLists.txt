# Desktop tests (README "Test"). Labels: unit, headless, cmake, script, windowed (needs Vulkan and a
# display), novulkan (Linux: runs the app with no Vulkan driver).

set(DESKTOP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---- Unit tests: model + viewmodel, no Raven, no window. ----
add_executable(tradelab_desktop_unit_tests
  main.cpp
  model/test_json.cpp
  model/test_i18n.cpp
  model/test_engine_host.cpp
  viewmodel/test_format.cpp
  viewmodel/test_chart_geometry.cpp
  viewmodel/test_app_viewmodel.cpp
  viewmodel/test_vulkan_message.cpp
  viewmodel/game/test_builder.cpp
  viewmodel/game/test_game_viewmodel.cpp
  viewmodel/game/test_options_parity.cpp
  viewmodel/game/test_rules.cpp
  viewmodel/game/test_sizing.cpp
  viewmodel/game/test_ticket_chart.cpp
  viewmodel/test_shell_helpers.cpp
  viewmodel/test_shell_screens.cpp
  viewmodel/test_pack_runs.cpp
  viewmodel/test_flows.cpp
  viewmodel/test_pack_screens.cpp
  viewmodel/test_theme.cpp
  viewmodel/test_web_parity.cpp
  viewmodel/test_academy_parity.cpp
  viewmodel/test_academy.cpp
  viewmodel/test_audio.cpp)
target_include_directories(tradelab_desktop_unit_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
target_include_directories(tradelab_desktop_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tradelab_desktop_unit_tests PRIVATE tradelab_desktop_viewmodel)
add_test(NAME desktop_unit COMMAND tradelab_desktop_unit_tests)
set_tests_properties(desktop_unit PROPERTIES LABELS unit)

# ---- Storage and packs (unit label): SQLite repository, recorder/resume, OFF02 crash child, packs. ----
# The crash child plays a session through EngineHost + SQLite and SIGKILLs itself at a TRADELAB_FAULT point.
add_executable(tradelab_desktop_crash_child storage/crash_child.cpp)
target_include_directories(tradelab_desktop_crash_child PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tradelab_desktop_crash_child PRIVATE tradelab_desktop_model)
target_sources(tradelab_desktop_unit_tests PRIVATE
  storage/test_run_store.cpp
  storage/test_recorder.cpp
  storage/test_crash_recovery.cpp
  storage/test_scripted_pack.cpp
  storage/test_profile_store.cpp
  storage/test_assisted.cpp
  packs/test_packs.cpp
  packs/test_download.cpp
  packs/test_pack_service.cpp
  update/test_updater.cpp)
add_dependencies(tradelab_desktop_unit_tests tradelab_desktop_crash_child)
target_compile_definitions(tradelab_desktop_unit_tests PRIVATE
  TRADELAB_CRASH_CHILD="$<TARGET_FILE:tradelab_desktop_crash_child>"
  TRADELAB_WEB_CATALOG_DIR="${DESKTOP_DIR}/../web/static/catalog"
  TRADELAB_WEB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/web")

# ---- Goldens (task 1.6): the engine's shared fixtures replayed through the desktop build. ----
# Its own binary so a universal macOS build can run it on each slice (arch -arm64 / -x86_64).
add_executable(tradelab_desktop_golden_tests main.cpp golden/test_golden.cpp)
target_include_directories(tradelab_desktop_golden_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
target_include_directories(tradelab_desktop_golden_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tradelab_desktop_golden_tests PRIVATE tradelab_desktop_model)
target_compile_definitions(tradelab_desktop_golden_tests PRIVATE
  TRADELAB_GOLDEN_DIR="${DESKTOP_DIR}/../../engine/tests/golden"
  TRADELAB_WEB_CATALOG_DIR="${DESKTOP_DIR}/../web/static/catalog")
add_test(NAME desktop_golden COMMAND tradelab_desktop_golden_tests)
set_tests_properties(desktop_golden PROPERTIES LABELS golden)

# ---- Headless: Raven null platform + null backend, every view loaded and m01 played to the debrief. ----
add_executable(tradelab_desktop_headless_tests main.cpp headless/test_headless.cpp headless/test_game_headless.cpp
  headless/test_headless_shell.cpp headless/test_headless_flows.cpp headless/test_headless_academy.cpp
  headless/test_headless_strategies.cpp)
target_include_directories(tradelab_desktop_headless_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
target_include_directories(tradelab_desktop_headless_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tradelab_desktop_headless_tests PRIVATE tradelab_desktop_view)
target_compile_definitions(tradelab_desktop_headless_tests PRIVATE TRADELAB_WEB_CATALOG_DIR="${DESKTOP_DIR}/../web/static/catalog"
  TRADELAB_WEB_AUDIO_DIR="${DESKTOP_DIR}/../web/static/audio")
add_test(NAME desktop_headless COMMAND tradelab_desktop_headless_tests)
# TRADELAB_AUDIO=null: the app's soundtrack runs on miniaudio's null device (decoded and mixed, not heard).
set_tests_properties(desktop_headless PROPERTIES LABELS headless ENVIRONMENT "RAVEN_AUTO_LOAD=0;RAVEN_LIVE_RELOAD=0;TRADELAB_AUDIO=null;TRADELAB_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/headless")

# ---- Windowed: the real Vulkan renderer driven by injected input; frames saved as PNG. ----
if(RAVEN_BACKEND_VULKAN)
  add_executable(tradelab_desktop_windowed_tests main.cpp windowed/test_windowed.cpp windowed/test_game_windowed.cpp
    windowed/test_windowed_shell.cpp windowed/test_windowed_flows.cpp windowed/test_windowed_playthroughs.cpp
    windowed/test_windowed_academy.cpp
    windowed/test_windowed_strategies.cpp
    support/png.cpp support/reference.cpp)
  target_compile_definitions(tradelab_desktop_windowed_tests PRIVATE
    TRADELAB_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/windowed/reference"
    TRADELAB_WEB_CATALOG_DIR="${DESKTOP_DIR}/../web/static/catalog")
  target_include_directories(tradelab_desktop_windowed_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
  target_include_directories(tradelab_desktop_windowed_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(tradelab_desktop_windowed_tests PRIVATE tradelab_desktop_view)
  target_compile_definitions(tradelab_desktop_windowed_tests PRIVATE TRADELAB_WEB_CATALOG_DIR="${DESKTOP_DIR}/../web/static/catalog")
  find_package(ZLIB)
  if(ZLIB_FOUND)
    target_compile_definitions(tradelab_desktop_windowed_tests PRIVATE TRADELAB_HAVE_ZLIB)
    target_link_libraries(tradelab_desktop_windowed_tests PRIVATE ZLIB::ZLIB)
  endif()
  add_test(NAME desktop_windowed COMMAND tradelab_desktop_windowed_tests)
  set_tests_properties(desktop_windowed PROPERTIES LABELS windowed
    ENVIRONMENT "TRADELAB_CAPTURE_DIR=${CMAKE_BINARY_DIR}/captures;RAVEN_AUTO_LOAD=0;RAVEN_LIVE_RELOAD=0;TRADELAB_AUDIO=null;TRADELAB_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/windowed")

  # Playtest: the twelve missions to the debrief with captures and text reports (README "Playtest").
  add_executable(tradelab_desktop_playtest_tests main.cpp windowed/test_playtest.cpp support/png.cpp)
  target_include_directories(tradelab_desktop_playtest_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
  target_include_directories(tradelab_desktop_playtest_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(tradelab_desktop_playtest_tests PRIVATE tradelab_desktop_view)
  if(ZLIB_FOUND)
    target_compile_definitions(tradelab_desktop_playtest_tests PRIVATE TRADELAB_HAVE_ZLIB)
    target_link_libraries(tradelab_desktop_playtest_tests PRIVATE ZLIB::ZLIB)
  endif()
  add_test(NAME desktop_playtest COMMAND tradelab_desktop_playtest_tests)
  set_tests_properties(desktop_playtest PROPERTIES LABELS "windowed;playtest"
    ENVIRONMENT "TRADELAB_CAPTURE_DIR=${CMAKE_BINARY_DIR}/captures;RAVEN_AUTO_LOAD=0;RAVEN_LIVE_RELOAD=0;TRADELAB_AUDIO=null;TRADELAB_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/playtest")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # No usable driver: an ICD list that names nothing. (CI also runs it with libvulkan1 removed.)
    add_test(NAME desktop_vulkan_missing
      COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:tradelab_desktop> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vulkan_missing.cmake)
    set_tests_properties(desktop_vulkan_missing PROPERTIES LABELS novulkan
      ENVIRONMENT "VK_ICD_FILENAMES=/nonexistent/icd.json;VK_DRIVER_FILES=/nonexistent/icd.json")
  endif()
endif()

# ---- Build rules as tests. ----
# Global -ffast-math must fail the configure step, naming the flag (spec "Fast-math refused").
add_test(NAME desktop_fast_math_refused
  COMMAND ${CMAKE_COMMAND} -S ${DESKTOP_DIR} -B ${CMAKE_BINARY_DIR}/probe-fast-math -G ${CMAKE_GENERATOR}
          "-DCMAKE_CXX_FLAGS=-O2 -ffast-math")
set_tests_properties(desktop_fast_math_refused PROPERTIES LABELS cmake
  PASS_REGULAR_EXPRESSION "CMAKE_CXX_FLAGS contains -ffast-math")

# A viewmodel source including a Raven header must not compile (spec "Layer boundary"); the same file
# compiles in the view layer, so the failure is the layering and nothing else.
add_library(tradelab_layer_probe_viewmodel OBJECT EXCLUDE_FROM_ALL cmake/includes_raven.cpp)
target_link_libraries(tradelab_layer_probe_viewmodel PRIVATE tradelab_desktop_viewmodel)
add_library(tradelab_layer_probe_view OBJECT EXCLUDE_FROM_ALL cmake/includes_raven.cpp)
target_link_libraries(tradelab_layer_probe_view PRIVATE tradelab_desktop_view)
add_test(NAME desktop_layer_viewmodel_refuses_raven
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tradelab_layer_probe_viewmodel)
set_tests_properties(desktop_layer_viewmodel_refuses_raven PROPERTIES LABELS cmake WILL_FAIL TRUE)
add_test(NAME desktop_layer_view_accepts_raven
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tradelab_layer_probe_view)
set_tests_properties(desktop_layer_view_accepts_raven PROPERTIES LABELS cmake)

# ---- Scripts (Python and Node, when available). ----
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME desktop_raven_pin COMMAND ${Python3_EXECUTABLE} ${DESKTOP_DIR}/scripts/check_raven_pin.py)
  add_test(NAME desktop_raven_pin_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/scripts -p "test_*.py")
  set_tests_properties(desktop_raven_pin desktop_raven_pin_tests PROPERTIES LABELS script)
endif()
