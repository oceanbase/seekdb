# macOS 27 uses a host-installed LLVM and a matching Apple SDK/linker.
# Keep DEVTOOLS_DIR for the separately packaged bison and flex tools.
set(OB_MACOS27 ON)
set(OB_MACOS_DEVELOPER_DIR "" CACHE PATH "Xcode/Command Line Tools developer directory for macOS 27")
set(OB_MACOS_LLVM_ROOT "" CACHE PATH "Optional host LLVM prefix (for example: brew --prefix llvm)")

if(NOT OB_MACOS_DEVELOPER_DIR)
  if(NOT "$ENV{DEVELOPER_DIR}" STREQUAL "")
    set(_macos_developer_candidates "$ENV{DEVELOPER_DIR}")
  else()
    execute_process(COMMAND xcode-select -p
      OUTPUT_VARIABLE _macos_active_developer OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_macos_developer_candidates
      "${_macos_active_developer}" "/Library/Developer/CommandLineTools")
  endif()
  foreach(_developer IN LISTS _macos_developer_candidates)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "DEVELOPER_DIR=${_developer}"
        xcrun --sdk macosx --show-sdk-version
      RESULT_VARIABLE _sdk_result OUTPUT_VARIABLE _sdk_version
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_sdk_result EQUAL 0 AND _sdk_version VERSION_GREATER_EQUAL 27)
      set(OB_MACOS_DEVELOPER_DIR "${_developer}" CACHE PATH
        "Xcode/Command Line Tools developer directory for macOS 27" FORCE)
      break()
    endif()
  endforeach()
endif()
if(NOT OB_MACOS_DEVELOPER_DIR)
  message(FATAL_ERROR "macOS 27 requires Xcode/Command Line Tools with SDK 27 or newer. "
    "Install it or set -DOB_MACOS_DEVELOPER_DIR=/path/to/Contents/Developer.")
endif()

function(ob_macos27_xcrun output)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "DEVELOPER_DIR=${OB_MACOS_DEVELOPER_DIR}"
      xcrun --sdk macosx ${ARGN}
    RESULT_VARIABLE _result OUTPUT_VARIABLE _value
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Cannot resolve macOS 27 toolchain: ${_error}")
  endif()
  set(${output} "${_value}" PARENT_SCOPE)
endfunction()

ob_macos27_xcrun(_macos_sdk_version --show-sdk-version)
if(_macos_sdk_version VERSION_LESS 27)
  message(FATAL_ERROR "${OB_MACOS_DEVELOPER_DIR} provides SDK ${_macos_sdk_version}; SDK 27 or newer is required.")
endif()
if(NOT CMAKE_OSX_SYSROOT)
  ob_macos27_xcrun(_macos_sdk --show-sdk-path)
  set(CMAKE_OSX_SYSROOT "${_macos_sdk}" CACHE PATH "macOS SDK" FORCE)
endif()
if(NOT EXISTS "${CMAKE_OSX_SYSROOT}/SDKSettings.json")
  message(FATAL_ERROR "Set CMAKE_OSX_SYSROOT to an absolute macOS SDK 27+ path.")
endif()
file(READ "${CMAKE_OSX_SYSROOT}/SDKSettings.json" _macos_sdk_settings)
string(JSON _macos_selected_sdk_version GET "${_macos_sdk_settings}" Version)
if(_macos_selected_sdk_version VERSION_LESS 27)
  message(FATAL_ERROR "macOS 27 build requires SDK 27 or newer, got ${CMAKE_OSX_SYSROOT}.")
endif()

foreach(_lang CC CXX)
  if(_lang STREQUAL "CC")
    set(_tool clang)
  else()
    set(_tool clang++)
  endif()
  if(OB_MACOS_LLVM_ROOT)
    set(OB_${_lang} "${OB_MACOS_LLVM_ROOT}/bin/${_tool}")
    set(_OB_MACOS_AUTO_${_lang} "${OB_${_lang}}" CACHE INTERNAL "Automatically selected host compiler" FORCE)
  elseif(NOT OB_${_lang} OR "${OB_${_lang}}" MATCHES "^${DEVTOOLS_DIR}/"
      OR "${OB_${_lang}}" STREQUAL "${_OB_MACOS_AUTO_${_lang}}")
    # Migrate caches made by the old macos15 dependency profile as well.
    ob_macos27_xcrun(OB_${_lang} --find ${_tool})
    set(_OB_MACOS_AUTO_${_lang} "${OB_${_lang}}" CACHE INTERNAL "Automatically selected host compiler" FORCE)
  endif()
  if(NOT EXISTS "${OB_${_lang}}")
    message(FATAL_ERROR "Host compiler not found: ${OB_${_lang}}")
  endif()
  set(OB_${_lang} "${OB_${_lang}}" CACHE FILEPATH "Host macOS compiler" FORCE)
endforeach()
set(CMAKE_ASM_COMPILER "${OB_CC}" CACHE FILEPATH "Assembler driver" FORCE)
get_filename_component(_macos_compiler_bin "${OB_CC}" DIRECTORY)
get_filename_component(CMAKE_TOOLCHAIN_PATH "${_macos_compiler_bin}" DIRECTORY)

ob_macos27_xcrun(OB_LD_BIN --find ld)
set(CMAKE_LINKER "${OB_LD_BIN}" CACHE FILEPATH "macOS linker" FORCE)
foreach(_tool ar ranlib strip nm)
  ob_macos27_xcrun(_macos_tool --find ${_tool})
  string(TOUPPER "${_tool}" _tool_upper)
  set(CMAKE_${_tool_upper} "${_macos_tool}" CACHE FILEPATH "macOS ${_tool}" FORCE)
endforeach()
# Darwin ld -r localizes private extern symbols; no llvm-objcopy is needed.
set(OB_OBJCOPY_BIN "")
message(STATUS "macOS developer tools: ${OB_MACOS_DEVELOPER_DIR}")
message(STATUS "macOS SDK: ${CMAKE_OSX_SYSROOT}")
message(STATUS "macOS linker: ${OB_LD_BIN}")
