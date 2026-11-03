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
  src/det_math.cpp
  src/fixed.cpp
  src/prng.cpp
  src/sha256.cpp
  src/serialize.cpp
)
add_library(tradelab::engine ALIAS tradelab_engine)
target_include_directories(tradelab_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(tradelab_engine PRIVATE
  ${TRADELAB_DETERMINISM_FLAGS} -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
if(EMSCRIPTEN)
  target_compile_options(tradelab_engine PRIVATE -fno-exceptions -fno-rtti)
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
    tests/test_prng.cpp
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
