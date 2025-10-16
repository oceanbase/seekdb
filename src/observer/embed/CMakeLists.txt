
if(BUILD_EMBED_MODE)
  # Set target Python version, can be overridden by CMake parameter
  if(NOT DEFINED PYTHON_VERSION)
    set(PYTHON_VERSION "3.8")
  endif()
  set(Python3_FOUND FALSE)

  message(STATUS "Target Python version: ${PYTHON_VERSION}")

  # First try to find the specified Python version from system
  message(STATUS "Searching for Python ${PYTHON_VERSION} in system first...")
  find_package(Python3 ${PYTHON_VERSION} EXACT COMPONENTS Interpreter Development QUIET)
  if(Python3_FOUND)
    message(STATUS "Found system Python ${PYTHON_VERSION}, using it for embed module")
  endif()

  # If system doesn't have the specified Python version, try to find from pyenv
  if(NOT Python3_FOUND)
    message(STATUS "No system Python ${PYTHON_VERSION} found, trying pyenv...")

    # Method 1: Check if pyenv version can be specified via environment variable
    if(DEFINED ENV{PYENV_VERSION})
      execute_process(
        COMMAND pyenv which python
        OUTPUT_VARIABLE PYENV_PYTHON_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE pyenv_result
      )
      if(pyenv_result EQUAL 0)
        # Verify if this Python version matches the target version
        execute_process(
          COMMAND ${PYENV_PYTHON_PATH} --version
          OUTPUT_VARIABLE PYENV_PYTHON_VERSION_OUTPUT
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(PYENV_PYTHON_VERSION_OUTPUT MATCHES "Python ${PYTHON_VERSION}")
          set(Python3_EXECUTABLE ${PYENV_PYTHON_PATH})
          message(STATUS "Using pyenv Python from PYENV_VERSION: ${Python3_EXECUTABLE} (${PYENV_PYTHON_VERSION_OUTPUT})")
          set(Python3_FOUND TRUE)
        else()
          message(STATUS "pyenv Python version mismatch: expected ${PYTHON_VERSION}, got ${PYENV_PYTHON_VERSION_OUTPUT}")
        endif()
      endif()
    endif()

    # Method 2: Directly try to find the specified version in pyenv
    if(NOT Python3_FOUND)
      execute_process(
        COMMAND pyenv versions --bare
        OUTPUT_VARIABLE PYENV_VERSIONS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE pyenv_versions_result
      )
      if(pyenv_versions_result EQUAL 0)
        # Check if there's a matching version
        string(FIND "${PYENV_VERSIONS}" "${PYTHON_VERSION}" VERSION_FOUND)
        if(NOT VERSION_FOUND EQUAL -1)
          # Try to use the specified version by setting PYENV_VERSION environment variable
          execute_process(
            COMMAND bash -c "export PYENV_VERSION=${PYTHON_VERSION} && pyenv which python"
            OUTPUT_VARIABLE PYENV_SPECIFIC_PYTHON_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE pyenv_specific_result
          )
          if(pyenv_specific_result EQUAL 0)
            # Verify the version matches
            execute_process(
              COMMAND ${PYENV_SPECIFIC_PYTHON_PATH} --version
              OUTPUT_VARIABLE PYENV_SPECIFIC_VERSION_OUTPUT
              OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(PYENV_SPECIFIC_VERSION_OUTPUT MATCHES "Python ${PYTHON_VERSION}")
              set(Python3_EXECUTABLE ${PYENV_SPECIFIC_PYTHON_PATH})
              message(STATUS "Using pyenv Python ${PYTHON_VERSION}: ${Python3_EXECUTABLE}")
              set(Python3_FOUND TRUE)
            else()
              message(STATUS "Version mismatch: expected ${PYTHON_VERSION}, got ${PYENV_SPECIFIC_VERSION_OUTPUT}")
            endif()
          endif()
        else()
          message(STATUS "Python ${PYTHON_VERSION} not found in pyenv versions: ${PYENV_VERSIONS}")
        endif()
      endif()
    endif()

    # Method 3: Directly find the specified version through PYENV_ROOT
    if(NOT Python3_FOUND AND DEFINED ENV{PYENV_ROOT})
      set(PYENV_SPECIFIC_PYTHON "$ENV{PYENV_ROOT}/versions/${PYTHON_VERSION}/bin/python")
      if(EXISTS ${PYENV_SPECIFIC_PYTHON})
        set(Python3_EXECUTABLE ${PYENV_SPECIFIC_PYTHON})
        message(STATUS "Using pyenv Python from PYENV_ROOT: ${Python3_EXECUTABLE}")
        set(Python3_FOUND TRUE)
      endif()
    endif()
  endif()

  # If still no suitable Python version found, error and exit
  if(NOT Python3_FOUND)
    message(FATAL_ERROR "No suitable Python ${PYTHON_VERSION} found. Please install Python ${PYTHON_VERSION} or configure pyenv with Python ${PYTHON_VERSION}.")
  endif()

  # Verify the final Python choice
  if(Python3_EXECUTABLE)
    execute_process(
      COMMAND ${Python3_EXECUTABLE} --version
      OUTPUT_VARIABLE PYTHON_VERSION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Final Python choice: ${Python3_EXECUTABLE} (${PYTHON_VERSION_OUTPUT})")
  endif()

  message(STATUS "Building embed module with Python ${Python3_EXECUTABLE}")
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
