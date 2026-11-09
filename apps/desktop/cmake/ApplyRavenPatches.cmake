# Applies third_party/raven-patches/*.patch to the Raven checkout, in name order. Run as the PATCH_COMMAND
# of the Raven FetchContent, in its source folder: a patch already applied is skipped, so a re-run of the
# patch step (a reconfigure, a cached _deps folder) changes nothing.
#
#   cmake -DPATCH_DIR=<folder> -P ApplyRavenPatches.cmake
file(GLOB patches "${PATCH_DIR}/*.patch")
list(SORT patches)
foreach(patch IN LISTS patches)
  execute_process(COMMAND git apply --reverse --check "${patch}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
  if(applied EQUAL 0)
    continue()
  endif()
  execute_process(COMMAND git apply "${patch}" RESULT_VARIABLE failed)
  if(NOT failed EQUAL 0)
    message(FATAL_ERROR "Raven patch does not apply to the pinned commit: ${patch}")
  endif()
endforeach()
