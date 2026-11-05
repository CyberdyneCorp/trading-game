# The engine compiles with its own PRIVATE determinism flags (-ffp-contract=off -fno-fast-math
# -fno-rounding-math). Global flags reach every target, the engine included, so the desktop project
# refuses any global flag that relaxes IEEE semantics (design: "Engine linkage").

set(TRADELAB_FORBIDDEN_FLOAT_FLAGS -ffast-math -Ofast -funsafe-math-optimizations -ffp-contract=fast)

function(tradelab_check_float_flags where value)
  foreach(flag IN LISTS TRADELAB_FORBIDDEN_FLOAT_FLAGS)
    string(FIND " ${value} " " ${flag} " hit)
    if(NOT hit EQUAL -1)
      message(FATAL_ERROR
        "${where} contains ${flag}: global floating-point flags would reach tradelab::engine and break "
        "its determinism (golden digests). Remove ${flag}.")
    endif()
  endforeach()
endfunction()

# Global compiler flags, for every configuration.
function(tradelab_check_global_float_flags)
  set(vars CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_OBJC_FLAGS)
  foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    list(APPEND vars CMAKE_C_FLAGS_${config} CMAKE_CXX_FLAGS_${config})
  endforeach()
  foreach(var IN LISTS vars)
    tradelab_check_float_flags(${var} "${${var}}")
  endforeach()
endfunction()

# Directory-wide options (add_compile_options) of a directory: they reach the targets below it.
function(tradelab_check_directory_float_flags dir)
  get_directory_property(options DIRECTORY ${dir} COMPILE_OPTIONS)
  string(REPLACE ";" " " options "${options}")
  tradelab_check_float_flags("COMPILE_OPTIONS of ${dir}" "${options}")
endfunction()
