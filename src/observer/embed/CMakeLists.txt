
if(BUILD_EMBED_MODE)
  find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
  execute_process(
      COMMAND "${Python3_EXECUTABLE}" -c 
              "import pybind11; print(pybind11.get_cmake_dir())"
      OUTPUT_VARIABLE pybind11_CMAKE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE result
  )
  
  if(NOT result EQUAL 0)
      message(STATUS "pybind11 not found, attempting to install automatically...")
      execute_process(
          COMMAND "${Python3_EXECUTABLE}" -m pip install --user pybind11
          RESULT_VARIABLE install_result
          OUTPUT_VARIABLE install_output
          ERROR_VARIABLE install_error
      )
      if(NOT install_result EQUAL 0)
          message(WARNING "Failed to install pybind11 automatically: ${install_error}")
          message(FATAL_ERROR "Please install pybind11 manually: pip3 install --user pybind11")
      else()
          message(STATUS "Successfully installed pybind11")
          execute_process(
              COMMAND "${Python3_EXECUTABLE}" -c 
                      "import pybind11; print(pybind11.get_cmake_dir())"
              OUTPUT_VARIABLE pybind11_CMAKE_DIR
              OUTPUT_STRIP_TRAILING_WHITESPACE
              RESULT_VARIABLE result
          )
          if(NOT result EQUAL 0)
              message(FATAL_ERROR "Failed to get pybind11 path after installation: ${result}")
          endif()
      endif()
  endif()
  message(STATUS "Found pybind11 cmake dir: ${pybind11_CMAKE_DIR}")
  set(pybind11_DIR ${pybind11_CMAKE_DIR})
  find_package(pybind11 CONFIG REQUIRED)
  message(STATUS "pybind11 version: ${pybind11_VERSION}")
  
  pybind11_add_module(oblite NO_EXTRAS python/ob_embed_impl.cpp)
  set_target_properties(oblite PROPERTIES
      PREFIX ""
      SUFFIX ".so"
      OUTPUT_NAME "oblite"
    )
  target_link_libraries(oblite PUBLIC
    oceanbase_static
    -static-libgcc
    -static-libstdc++
  )
  if (OB_USE_LLD)
    target_link_options(oblite PRIVATE -fuse-ld=${DEVTOOLS_DIR}/bin/ld.lld)
  endif()
endif()
