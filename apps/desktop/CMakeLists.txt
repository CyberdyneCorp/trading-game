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

# Resources embedded in the binary (views, generated catalogs).
set(TRADELAB_DESKTOP_RESOURCES
  views/menu.xml views/briefing.xml views/game.xml views/debrief.xml
  i18n/strings.csv packs/pinned_keys.json)
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
  src/viewmodel/game/options_math.cpp
  src/viewmodel/game/options_vm.cpp
  src/viewmodel/game/panels_vm.cpp
  src/viewmodel/game/progress.cpp
  src/viewmodel/game/shortcuts.cpp
  src/viewmodel/game/sizing.cpp
  src/viewmodel/game/texts.cpp
  src/viewmodel/game/ticket_vm.cpp)
set(TRADELAB_DESKTOP_GAME_VIEW_SOURCES)

# ---- Layers (design "Library split enforces the layers"): model and viewmodel never see Raven. ----

add_library(tradelab_desktop_model STATIC
  src/model/engine_host.cpp
  src/model/engine_run.cpp
  src/model/game_command.cpp
  src/model/game_prefs.cpp
  src/model/game_state.cpp
  src/model/i18n.cpp
  src/model/json.cpp
  ${CMAKE_BINARY_DIR}/generated/resources.cpp)
target_include_directories(tradelab_desktop_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(tradelab_desktop_model PUBLIC tradelab::engine Threads::Threads)
target_compile_options(tradelab_desktop_model PRIVATE ${TRADELAB_DESKTOP_WARNINGS})

add_library(tradelab_desktop_viewmodel STATIC
  src/viewmodel/app_viewmodel.cpp
  src/viewmodel/chart_geometry.cpp
  src/viewmodel/format.cpp
  src/viewmodel/screen_texts.cpp
  src/viewmodel/vulkan_message.cpp
  ${TRADELAB_DESKTOP_GAME_VIEWMODEL_SOURCES})
target_link_libraries(tradelab_desktop_viewmodel PUBLIC tradelab_desktop_model)
target_compile_options(tradelab_desktop_viewmodel PRIVATE ${TRADELAB_DESKTOP_WARNINGS})

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
  src/view/raven/raven_ui.cpp)
target_link_libraries(tradelab_desktop_view PUBLIC tradelab_desktop_viewmodel tradelab_desktop_platform raven::raven)
target_compile_options(tradelab_desktop_view PRIVATE ${TRADELAB_DESKTOP_WARNINGS})

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
