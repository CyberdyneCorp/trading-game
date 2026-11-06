# Runs the app where no Vulkan driver can be used (spec "Vulkan runtime requirement"): it must print the
# localized message to stderr and exit with status 3, never crash or exit silently.
#   cmake -DAPP=<tradelab binary> -P vulkan_missing.cmake

set(ENV{TRADELAB_NO_DIALOG} 1)
foreach(case "pt_BR.UTF-8;O TradeLab precisa de um driver Vulkan;Instale `libvulkan1`"
              "en_US.UTF-8;TradeLab needs a Vulkan driver;Install `libvulkan1`")
  list(GET case 0 lang)
  list(GET case 1 reason)
  list(GET case 2 fix)
  set(ENV{LANG} ${lang})
  set(ENV{LC_ALL} ${lang})
  execute_process(COMMAND ${APP} RESULT_VARIABLE code ERROR_VARIABLE err OUTPUT_VARIABLE out TIMEOUT 60)
  if(NOT code EQUAL 3)
    message(FATAL_ERROR "[${lang}] expected exit status 3, got '${code}'\nstderr:\n${err}")
  endif()
  foreach(text IN ITEMS "${reason}" "${fix}")
    string(FIND "${err}" "${text}" at)
    if(at EQUAL -1)
      message(FATAL_ERROR "[${lang}] stderr does not contain '${text}':\n${err}")
    endif()
  endforeach()
  message(STATUS "[${lang}] exit 3 with the Vulkan message")
endforeach()
