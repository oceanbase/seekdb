oblib_add_library(compress ${SEEKDB_OBLIB_COMPRESS_STANDALONE})

# On macOS, the architecture must be explicitly specified
set(ARCH_ARGS "")
set(OB_DEPLOY "")
if(APPLE)
  # preferably using CMake's CMAKE_OSX_ARCHITECTURES configuration
  if(CMAKE_OSX_ARCHITECTURES)
      set(ARCH_ARGS -arch ${CMAKE_OSX_ARCHITECTURES})
  else()
      set(ARCH_ARGS -arch ${ARCHITECTURE})
  endif()
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
  set(OB_DEPLOY -platform_version macos ${CMAKE_OSX_DEPLOYMENT_TARGET} ${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

add_subdirectory(zstd_1_3_8)

target_link_libraries(compress PUBLIC oblib_base zstd_1_3_8)
