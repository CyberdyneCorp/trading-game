# Third-party code fetched at configure time, every item pinned by commit sha or archive hash.
#
# Offline or cached builds: pass -DFETCHCONTENT_SOURCE_DIR_RAVEN=<clone at the pinned sha> and
# -DFETCHCONTENT_SOURCE_DIR_RAVEN_VULKAN_HEADERS=<Vulkan-Headers checkout> (and
# FETCHCONTENT_SOURCE_DIR_DOCTEST for the tests), or reuse a build folder's _deps.

include(FetchContent)

# Raven (Apache-2.0). Pre-1.0 and changing daily upstream: always a full commit sha, never a branch
# or a tag. Bumping = change this line, apps/desktop/README.md and the add-native-clients design
# together (scripts/check_raven_pin.py fails CI otherwise), in a PR of its own.
set(TRADELAB_RAVEN_SHA 0fc738631c33844d6da732210267d21b5223f960)
set(TRADELAB_RAVEN_URL https://github.com/oscar-soirey/Raven.git)
option(TRADELAB_RAVEN_MIRROR "Fetch Raven from the organization's read-only mirror instead of upstream" OFF)
set(TRADELAB_RAVEN_MIRROR_URL https://github.com/CyberdyneCorp/raven-mirror.git CACHE STRING
    "Read-only mirror of the pinned Raven commit")
if(TRADELAB_RAVEN_MIRROR)
  set(TRADELAB_RAVEN_URL ${TRADELAB_RAVEN_MIRROR_URL})
endif()

# Vulkan-Headers v1.3.290, the tag cmake/RavenVulkanHeaders.cmake asks for, by its commit. Declared
# before Raven so this sha-pinned declaration wins over Raven's tag-based one.
set(TRADELAB_VULKAN_HEADERS_SHA b379292b2ab6df5771ba9870d53cf8b2c9295daf)

option(TRADELAB_DESKTOP_HEADLESS "Raven null platform without Vulkan (CI machines without X11 or GPU)" OFF)

set(RAVEN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(RAVEN_WITH_SVG ON CACHE BOOL "" FORCE)
set(RAVEN_WITH_STB_IMAGE ON CACHE BOOL "" FORCE)
if(TRADELAB_DESKTOP_HEADLESS)
  set(RAVEN_PLATFORM null CACHE STRING "" FORCE)
  set(RAVEN_BACKEND_VULKAN OFF CACHE BOOL "" FORCE)
else()
  set(RAVEN_BACKEND_VULKAN ON CACHE BOOL "" FORCE)
endif()

if(RAVEN_BACKEND_VULKAN)
  # Only the headers: SOURCE_SUBDIR names a folder without CMakeLists.txt, so nothing is configured.
  FetchContent_Declare(raven_vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG ${TRADELAB_VULKAN_HEADERS_SHA}
    SOURCE_SUBDIR raven-headers-only)
  FetchContent_MakeAvailable(raven_vulkan_headers)
  set(RAVEN_VULKAN_INCLUDE_DIR "${raven_vulkan_headers_SOURCE_DIR}/include" CACHE PATH "" FORCE)
endif()

# Fixes that cannot wait for upstream (third_party/raven-patches, see the README). A source folder given
# with FETCHCONTENT_SOURCE_DIR_RAVEN is used as it is: apply the patches to it yourself.
FetchContent_Declare(raven
  GIT_REPOSITORY ${TRADELAB_RAVEN_URL}
  GIT_TAG ${TRADELAB_RAVEN_SHA}
  PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH_DIR=${CMAKE_CURRENT_LIST_DIR}/../third_party/raven-patches
                -P ${CMAKE_CURRENT_LIST_DIR}/ApplyRavenPatches.cmake)
FetchContent_MakeAvailable(raven)

if(TRADELAB_DESKTOP_BUILD_TESTS)
  # Same archive and hash as engine/CMakeLists.txt.
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR header-only-no-cmake)
  FetchContent_MakeAvailable(doctest)
endif()
