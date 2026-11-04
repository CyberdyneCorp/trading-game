cmake_minimum_required(VERSION 3.28)
project(tradelab_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)  # no C++ modules; scanning breaks with emscripten + CMake 4

option(TRADELAB_BUILD_TESTS "Build engine tests" ON)

# Determinism flags (design D3): no FMA contraction, no fast-math, IEEE semantics.
# x86-64 uses SSE2 by default (no x87); wasm has strict IEEE f64 by definition.
set(TRADELAB_DETERMINISM_FLAGS -ffp-contract=off -fno-fast-math)
if(NOT EMSCRIPTEN)
  list(APPEND TRADELAB_DETERMINISM_FLAGS -fno-rounding-math)
endif()

add_library(tradelab_engine STATIC
  src/accounting.cpp
  src/advisor.cpp
  src/c_api.cpp
  src/checkpoint.cpp
  src/advance.cpp
  src/commands.cpp
  src/det_math.cpp
  src/digest.cpp
  src/fixed.cpp
  src/generator.cpp
  src/indicators.cpp
  src/candle_patterns.cpp
  src/missions.cpp
  src/options_exec.cpp
  src/options_pricing.cpp
  src/prng.cpp
  src/scenario.cpp
  src/scoring.cpp
  src/debrief.cpp
  src/sha256.cpp
  src/serialize.cpp
)
add_library(tradelab::engine ALIAS tradelab_engine)
target_include_directories(tradelab_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(tradelab_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Shared library exposing the C ABI (c_api.h) for the Python binding used by the game server.
if(NOT EMSCRIPTEN)
  add_library(tradelab_capi SHARED src/capi_shared.cpp)
  target_link_libraries(tradelab_capi PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,tradelab::engine>")
  set_target_properties(tradelab_capi PROPERTIES OUTPUT_NAME tradelab)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(tradelab_capi PRIVATE -Wl,--no-undefined)  # fail at link time, not at dlopen
  endif()
endif()
target_compile_options(tradelab_engine PRIVATE
  ${TRADELAB_DETERMINISM_FLAGS} -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
if(EMSCRIPTEN)
  target_compile_options(tradelab_engine PRIVATE -fno-exceptions -fno-rtti)
endif()

# Browser module: ES6, single file (wasm embedded) for the Phase 1 slice; runs inside a Web Worker.
if(EMSCRIPTEN)
  add_executable(tradelab_wasm src/wasm_main.cpp)
  target_link_libraries(tradelab_wasm PRIVATE tradelab::engine)
  target_compile_options(tradelab_wasm PRIVATE ${TRADELAB_DETERMINISM_FLAGS} -fno-exceptions -fno-rtti)
  set_target_properties(tradelab_wasm PROPERTIES OUTPUT_NAME tradelab SUFFIX .mjs)
  target_link_options(tradelab_wasm PRIVATE
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createTradelab -sSINGLE_FILE=1
    -sENVIRONMENT=web,worker -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0
    "-sEXPORTED_FUNCTIONS=['_tl_version','_tl_missions','_tl_run_create','_tl_run_create_ex','_tl_run_destroy','_tl_request_advice','_tl_place_order','_tl_place_order_ex','_tl_place_option_order','_tl_adjust_margin','_tl_record_plan','_tl_answer_question','_tl_checkpoint','_tl_run_restore','_tl_cancel_order','_tl_set_leverage','_tl_close_position','_tl_advance','_tl_state','_tl_debrief','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']")
endif()

if(TRADELAB_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR header-only-no-cmake)  # header-only: do not run doctest's (CMake<3.5) build
  FetchContent_MakeAvailable(doctest)

  add_executable(tradelab_engine_tests
    tests/main.cpp
    tests/test_fixed.cpp
    tests/test_det_math.cpp
    tests/test_advisor.cpp
    tests/test_brackets.cpp
    tests/test_checkpoint.cpp
    tests/test_engine.cpp
    tests/test_indicators.cpp
    tests/test_missions.cpp
    tests/test_generator.cpp
    tests/test_candle_patterns.cpp
    tests/test_mission_realism.cpp
    tests/test_options.cpp
    tests/test_prng.cpp
    tests/test_scoring.cpp
    tests/test_debrief.cpp
    tests/test_sha256.cpp
    tests/test_serialize.cpp
  )
  target_compile_options(tradelab_engine_tests PRIVATE ${TRADELAB_DETERMINISM_FLAGS})
  if(EMSCRIPTEN)
    target_compile_definitions(tradelab_engine_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    target_link_options(tradelab_engine_tests PRIVATE -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node)
  endif()
  target_include_directories(tradelab_engine_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
  target_link_libraries(tradelab_engine_tests PRIVATE tradelab::engine)

  enable_testing()
  if(EMSCRIPTEN)
    add_test(NAME engine_tests COMMAND node $<TARGET_FILE:tradelab_engine_tests>)
  else()
    add_test(NAME engine_tests COMMAND tradelab_engine_tests)
  endif()

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME engine_banned_apis
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_engine_bans.py
              ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
  endif()
endif()
