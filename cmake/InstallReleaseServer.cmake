# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

function(seekdb_install_release_server target)
  # Packaging profiles register their full install layout in Pack.cmake.
  # Ordinary Linux Release builds still need a complete server component.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT SEEKDB_CMAKE_PACKAGE_BUILD)
    install(TARGETS "${target}" RUNTIME DESTINATION usr/bin COMPONENT server)
  endif()
endfunction()
