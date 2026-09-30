# Backport https://github.com/boostorg/mpl/pull/77 for the packaged Boost 1.74.
# New Clang rejects out-of-range enum casts in integral_c::next/prior as a
# hard error. The upstream fix defers those expressions via static constants.
# Generate an include overlay so shared/downloaded dependency trees stay intact.
set(_boost_wrapper "${DEP_DIR}/include/boost/mpl/aux_/integral_wrapper.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_boost_wrapper}")
file(READ "${_boost_wrapper}" _boost_wrapper_contents)
set(_boost_old_condition "#if BOOST_WORKAROUND(__EDG_VERSION__, <= 243)\n")
string(FIND "${_boost_wrapper_contents}" "${_boost_old_condition}" _boost_patch_pos)
if(NOT _boost_patch_pos EQUAL -1)
  string(REPLACE "${_boost_old_condition}"
    "#if BOOST_WORKAROUND(__EDG_VERSION__, <= 243) || __cplusplus >= 201103L\n"
    _boost_wrapper_contents "${_boost_wrapper_contents}")
  set(_boost_compat_include "${CMAKE_BINARY_DIR}/compat_include")
  file(MAKE_DIRECTORY "${_boost_compat_include}/boost/mpl/aux_")
  file(CONFIGURE
    OUTPUT "${_boost_compat_include}/boost/mpl/aux_/integral_wrapper.hpp"
    CONTENT "${_boost_wrapper_contents}" @ONLY)
  target_include_directories(oblib_base_without_pass BEFORE INTERFACE "${_boost_compat_include}")
endif()
