# Run a trusted Rust tool from the build, and replace only our generated output
# after successful generation. The input is the completed host executable.
set(_format_args)
set(_marker "SEEKDB_GENERATED_SERVER_DEV_CONTRACT_H_")
if(FORMAT STREQUAL "rust")
  set(_format_args --rust)
  set(_marker "SEEKDB_GENERATED_SERVER_DEV_CONTRACT_RS")
elseif(DEFINED FORMAT AND NOT FORMAT STREQUAL "c")
  message(FATAL_ERROR "unknown server-dev contract format")
endif()
execute_process(COMMAND "${TOOL}" "${HOST}" "-" ${_format_args}
  RESULT_VARIABLE _result OUTPUT_VARIABLE _header ERROR_VARIABLE _error)
if(NOT _result EQUAL 0 OR NOT _header MATCHES "${_marker}")
  message(FATAL_ERROR "server-dev contract generation failed: ${_error}")
endif()
# Preserve the timestamp for an unchanged linked identity. Rust targets run
# this check on every build, including host-path switches with older mtimes.
file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT "${_header}" @ONLY)
