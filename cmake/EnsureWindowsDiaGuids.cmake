# LLVM imports may reference BuildTools\DIA SDK\...\diaguids.lib; CI/home often only have Community/etc.
# Copy one existing VS 2022 diaguids.lib there before linking (must run before add_subdirectory(src)).

if(NOT WIN32)
  return()
endif()

# LLVM/seekdb Windows CI is x64 -> amd64 DIA libs; ARM64 host uses arm64.
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
  set(_a "arm64")
else()
  set(_a "amd64")
endif()

set(_dst "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/DIA SDK/lib/${_a}/diaguids.lib")
if(EXISTS "${_dst}")
  return()
endif()

set(_cand "")
if(DEFINED ENV{VSINSTALLDIR})
  file(TO_CMAKE_PATH "$ENV{VSINSTALLDIR}" _r)
  string(REGEX REPLACE "/+$" "" _r "${_r}")
  list(APPEND _cand "${_r}/DIA SDK/lib/${_a}/diaguids.lib")
endif()

set(_vw "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
if(EXISTS "${_vw}")
  execute_process(
    COMMAND "${_vw}" -latest -products * -utf8 -property installationPath
    OUTPUT_VARIABLE _vp OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _vr
  )
  if(_vr EQUAL 0 AND _vp)
    string(STRIP "${_vp}" _vp)
    list(APPEND _cand "${_vp}/DIA SDK/lib/${_a}/diaguids.lib")
  endif()
endif()

foreach(_root "C:/Program Files/Microsoft Visual Studio/2022" "C:/Program Files (x86)/Microsoft Visual Studio/2022")
  if(EXISTS "${_root}")
    file(GLOB _g "${_root}/*/DIA SDK/lib/${_a}/diaguids.lib")
    list(APPEND _cand ${_g})
  endif()
endforeach()

set(_src "")
foreach(_i IN LISTS _cand)
  if(EXISTS "${_i}")
    set(_src "${_i}")
    break()
  endif()
endforeach()

if(NOT _src)
  message(WARNING "EnsureWindowsDiaGuids: diaguids.lib (${_a}) not found. LLVM/lld link may fail.")
  return()
endif()

get_filename_component(_dd "${_dst}" DIRECTORY)
file(MAKE_DIRECTORY "${_dd}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${_src}" "${_dst}" RESULT_VARIABLE _ec)
if(NOT _ec EQUAL 0)
  message(WARNING "EnsureWindowsDiaGuids: copy failed (${_ec}); try elevated cmake or install DIA SDK.")
else()
  message(STATUS "EnsureWindowsDiaGuids: ${_src} -> ${_dst}")
endif()
