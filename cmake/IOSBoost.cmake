# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0

# Backport Boost.NumericConversion's enum-wrapper fix to the pinned 1.74 headers.
# Boost 1.85 uses integral_constant: MPL's next/prior instantiate values outside
# the enum range, which current Apple Clang rejects as non-constant expressions.
# Reference: boostorg/numeric_conversion, boost-1.85.0, include/boost/numeric/conversion.
function(seekdb_ios_boost_headers target)
  set(_include "${OB_HEADER_DEP_DIR}/include")
  if(NOT EXISTS "${_include}/boost/version.hpp")
    return()
  endif()
  file(STRINGS "${_include}/boost/version.hpp" _version REGEX "^#define BOOST_VERSION ")
  if(NOT _version STREQUAL "#define BOOST_VERSION 107400")
    return()
  endif()
  set(_overlay "${CMAKE_BINARY_DIR}/ios-compat/include")
  foreach(_header converter_policies.hpp detail/converter.hpp
      detail/int_float_mixture.hpp detail/udt_builtin_mixture.hpp detail/sign_mixture.hpp)
    set(_relative "boost/numeric/conversion/${_header}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_include}/${_relative}")
    file(READ "${_include}/${_relative}" _content)
    string(REPLACE "mpl::integral_c<" "boost::integral_constant<" _content "${_content}")
    string(PREPEND _content "#include <boost/type_traits/integral_constant.hpp>\n")
    get_filename_component(_directory "${_overlay}/${_relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${_directory}")
    # Avoid touching unchanged headers and rebuilding every dependent object.
    file(CONFIGURE OUTPUT "${_overlay}/${_relative}" CONTENT "${_content}" @ONLY)
  endforeach()
  target_include_directories(${target} BEFORE INTERFACE "${_overlay}")
endfunction()
