add_library(oblib_base INTERFACE)
add_library(oblib_base_without_pass INTERFACE)
add_library(oblib_pass INTERFACE)

get_property(EASY_INCLUDE_DIRS GLOBAL PROPERTY "EASY_INCLUDE_DIRS" )

if(WIN32)
  set(OPENSSL_INCLUDE_DIR "${OB_OPENSSL_DIR}/include")
  set(VCPKG_INCLUDE_DIR "${OB_VCPKG_DIR}/include")
  set(CURL_INCLUDE_DIR "${OB_VCPKG_DIR}/include")
  # vcpkg puts sqlite3.h at include/sqlite3.h, but code uses <sqlite/sqlite3.h>
  set(SQLITE_COMPAT_DIR "${CMAKE_BINARY_DIR}/compat_include")
  file(MAKE_DIRECTORY "${SQLITE_COMPAT_DIR}/sqlite")
  if(EXISTS "${OB_VCPKG_DIR}/include/sqlite3.h")
    configure_file("${OB_VCPKG_DIR}/include/sqlite3.h"
                   "${SQLITE_COMPAT_DIR}/sqlite/sqlite3.h" COPYONLY)
  endif()
endif()

target_include_directories(
  oblib_base_without_pass INTERFACE
  # oblib exposes no src-reachable -I -- not even the repo root ${CMAKE_SOURCE_DIR}.
  # So any include of src from oblib lib+unittests at compile time (both share/.. and full src/.. spellings)
  # is file-not-found. oblib itself needs no repo root: 0 src/.. ; 2 deps/.. resolved via same-directory.
  # src's repo root and src are provided by src's own base (see src/CMakeLists.txt); the grep guard becomes a redundant backstop.
  ${CMAKE_SOURCE_DIR}/deps/easy/src
  ${CMAKE_SOURCE_DIR}/deps/oblib/src
  ${CMAKE_SOURCE_DIR}/deps/oblib/src/common
  ${CMAKE_SOURCE_DIR}/deps/easy/src/include
  ${DEP_DIR}/include
  ${DEP_DIR}/include/libxml2
  $<$<NOT:$<PLATFORM_ID:Darwin>>:${SYS_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${SYS_UM_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${SYS_UCRT_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${SYS_SHARED_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${OPENSSL_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${VCPKG_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${VCPKG_INCLUDE_DIR}/libxml2>
  $<$<PLATFORM_ID:Windows>:${CURL_INCLUDE_DIR}>
  $<$<PLATFORM_ID:Windows>:${SQLITE_COMPAT_DIR}>
  $<$<PLATFORM_ID:Windows>:${OB_VSAG_DIR}/include>
  ${DEVTOOLS_DIR}
  ${DEVTOOLS_DIR}/include
  ${DEP_DIR}/include/icu
  ${DEP_DIR}/include/icu/common
  ${DEP_DIR}/include/fast_float
  ${USSL_INCLUDE_DIRS}
)

# -Wno-vla-cxx-extension is Clang 18+ only; GCC and older Clang use -Wno-vla-extension
include(CheckCXXCompilerFlag)
set(CMAKE_REQUIRED_FLAGS "-Werror")
check_cxx_compiler_flag("-Wno-vla-cxx-extension" COMPILER_SUPPORTS_VLA_CXX_EXTENSION)
unset(CMAKE_REQUIRED_FLAGS)
if(COMPILER_SUPPORTS_VLA_CXX_EXTENSION)
  set(VLA_WARN_FLAG "-Wno-vla-cxx-extension")
else()
  set(VLA_WARN_FLAG "-Wno-vla-extension")
endif()

if (OB_USE_CLANG)
  if(WIN32)
    set(OBLIB_COMPILE_DEFINITIONS /Oy- -fno-strict-aliasing ${MARCH_CFLAGS} ${MTUNE_CFLAGS}
      -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D_NO_EXCEPTION -D_USE_MATH_DEFINES
      -DOCI_LINK_RUNTIME -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH -DYY_NO_UNISTD_H
      -Wall -Wextra -Wformat -Werror
      -Wno-deprecated -Wno-address-of-packed-member -Wno-sign-compare -Wno-tautological-compare
      -Wno-psabi -Wno-c99-designator -Wno-int-in-bool-context -Wno-sizeof-array-div
      -Wno-implicit-const-int-float-conversion -Wno-fortify-source -Wno-non-c-typedef-for-linkage
      -Wno-uninitialized-const-reference -Wno-unused-function -Wno-atomic-alignment
      -Wno-tautological-constant-out-of-range-compare -Wno-unused-parameter
      -Wno-string-plus-int -Wno-misleading-indentation -Wno-unused-private-field
      $<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual -Wno-reserved-user-defined-literal
      -Wno-self-assign -Wno-inconsistent-missing-override -Wno-mismatched-tags -Wno-unused-variable
      -Wno-invalid-offsetof -Wno-unevaluated-expression -Wno-constant-logical-operand>
      $<$<COMPILE_LANGUAGE:C>:-Wno-unused-parameter>
      -Wno-unused-but-set-variable -Wno-null-pointer-subtraction -Wno-compound-token-split-by-macro
      -Wno-packed-non-pod -Wno-format -Wno-shorten-64-to-32 -Wno-unused-but-set-parameter -Wno-single-bit-bitfield-constant-conversion
      -Wno-covered-switch-default
      -Wno-bitwise-instead-of-logical -Wno-suggest-override -Wno-non-virtual-dtor -Wno-cast-qual
      -Wno-unused-value -Wno-macro-redefined -Wno-reinterpret-base-class -Wno-register
      -Wno-implicit-fallthrough -Wno-deprecated-non-prototype
      -Wno-ambiguous-reversed-operator -Wno-invalid-partial-specialization
      -Wno-string-concatenation
      /EHsc -Wno-missing-noreturn -Wno-undefined-func-template
      -Wno-undef -Wno-sign-conversion -Wno-strict-prototypes -Wno-switch-default -Wno-language-extension-token  
      -Wno-missing-prototypes -Wno-extra-semi-stmt -Wno-unused-macros -Wno-unsafe-buffer-usage -Wno-c++98-compat -Wno-c++98-compat-pedantic
      -Wno-reserved-identifier -Wno-padded -Wno-variadic-macros -Wno-global-constructors -Wno-undefined-reinterpret-cast
      -Wno-tautological-unsigned-zero-compare -Wno-integer-overflow -Wno-shift-count-overflow -Wno-cast-function-type -Wno-cast-function-type-strict
      -Wno-old-style-cast -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-documentation -Wno-microsoft-enum-value
      -Wno-gnu-zero-variadic-macro-arguments -Wno-gnu-statement-expression-from-macro-expansion
      -Wno-double-promotion -Wno-zero-as-null-pointer-constant -Wno-suggest-destructor-override -Wno-zero-length-array -Wno-extra-semi
      -Wno-exit-time-destructors -Wno-inconsistent-missing-destructor-override -Wno-shadow -Wno-unreachable-code
      -Wno-switch-enum -Wno-atomic-implicit-seq-cst -Wno-documentation-unknown-command
      -Wno-implicit-int-conversion -Wno-gnu-conditional-omitted-operand -Wno-cast-align -Wno-comma -Wno-tautological-type-limit-compare
      -Wno-disabled-macro-expansion -Wno-unreachable-code-break -Wno-missing-variable-declarations 
      -Wno-nonportable-system-include-path -Wno-c99-extensions -Wno-flexible-array-extensions 
      -Wno-compound-token-split-by-space -Wno-newline-eof -Wno-shadow-field -Wno-microsoft-unqualified-friend -Wno-float-equal
      -Wno-used-but-marked-unused -Wno-unreachable-code-return -Wno-float-conversion -Wno-conditional-uninitialized
      -Wno-signed-enum-bitfield -Wno-bitfield-enum-conversion -Wno-redundant-parens -Wno-gnu-statement-expression
      -Wno-bitfield-constant-conversion -Wno-gnu-redeclared-enum -Wno-header-hygiene
      -Wno-vla-cxx-extension -Wno-format-pedantic -Wno-implicit-int-float-conversion -Wno-undefined-inline -Wno-embedded-directive
      -Wno-nullability-extension -Wno-gcc-compat -Wno-thread-safety-negative -Wno-implicit-float-conversion
      -Wno-microsoft-cpp-macro -Wno-unused-exception-parameter -Wno-unused-template -Wno-tautological-value-range-compare
      -Wno-tautological-unsigned-enum-zero-compare -Wno-microsoft-template -Wno-format-nonliteral -Wno-c++20-compat
      -Wno-dtor-name -Wno-unused-member-function -Wno-string-conversion -Wno-declaration-after-statement -Wno-implicit-function-declaration
      -Wno-incompatible-pointer-types-discards-qualifiers -Wno-gnu-case-range
      -Wno-duplicate-enum)
  else()
    set(OBLIB_COMPILE_DEFINITIONS -fno-strict-aliasing -fno-omit-frame-pointer ${MARCH_CFLAGS} ${MTUNE_CFLAGS}
      -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D_NO_EXCEPTION
      -DOCI_LINK_RUNTIME
      -Wall -Wextra -Wformat -Werror
      -Wno-deprecated -Wno-address-of-packed-member -Wno-sign-compare -Wno-tautological-compare
      -Wno-psabi -Wno-c99-designator -Wno-int-in-bool-context -Wno-sizeof-array-div
      -Wno-implicit-const-int-float-conversion -Wno-fortify-source -Wno-non-c-typedef-for-linkage
      -Wno-uninitialized-const-reference -Wno-unused-function -Wno-atomic-alignment
      -Wno-tautological-constant-out-of-range-compare -Wno-unused-parameter
      -Wno-string-plus-int -Wno-misleading-indentation -Wno-unused-private-field
      $<$<COMPILE_LANGUAGE:CXX>:-Wno-overloaded-virtual -Wno-reserved-user-defined-literal
      -Wno-self-assign -Wno-inconsistent-missing-override -Wno-mismatched-tags -Wno-unused-variable
      -Wno-invalid-offsetof -Wno-unevaluated-expression -Wno-constant-logical-operand>
      $<$<COMPILE_LANGUAGE:C>:-Wno-unused-parameter>
      -Wno-unused-but-set-variable -Wno-null-pointer-subtraction -Wno-compound-token-split-by-macro
      -Wno-packed-non-pod -Wno-format -Wno-shorten-64-to-32 -Wno-unused-but-set-parameter -Wno-single-bit-bitfield-constant-conversion
      -Wno-enum-constexpr-conversion -Wno-covered-switch-default
      -Wno-bitwise-instead-of-logical -Wno-suggest-override -Wno-non-virtual-dtor -Wno-cast-qual
      -Wno-unused-value -Wno-macro-redefined -Wno-reinterpret-base-class -Wno-register
      -Wno-implicit-fallthrough -Wno-deprecated-non-prototype
      -Wno-ambiguous-reversed-operator -Wno-invalid-partial-specialization
      -Wno-string-concatenation
      -Wno-vla ${VLA_WARN_FLAG})
  endif()
  target_compile_options(oblib_base_without_pass INTERFACE ${OBLIB_COMPILE_DEFINITIONS})
else()
  target_compile_options(oblib_base_without_pass
    INTERFACE
    -fno-strict-aliasing -fno-omit-frame-pointer ${MARCH_CFLAGS} ${MTUNE_CFLAGS}
    -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D_NO_EXCEPTION
    -DOCI_LINK_RUNTIME
    -Wall -Wextra -Wformat -Werror
    -Wno-deprecated
    -Wno-sign-compare
    -Wno-psabi
    -Wno-attributes
    -Wno-nonnull-compare
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-switch
    -Wno-address-of-packed-member
    -Wno-ignored-qualifiers
    -Wno-format
    -Wno-type-limits
    -Wno-format-truncation
    -Wno-int-in-bool-context
    -Wno-cast-function-type
    -Wno-unused-function
    -Wno-overflow
    -Wno-unused-but-set-variable
    -Wno-memset-elt-size
    $<$<COMPILE_LANGUAGE:CXX>:
    -Wno-literal-suffix
    -Wno-invalid-offsetof
    -Wno-unused-variable
    -Wno-deprecated-copy
    -Wno-class-memaccess
    -Wno-unused-parameter
    -Wno-unused-but-set-variable
    -Wno-pragmas
    -Wno-maybe-uninitialized
    -Wno-subobject-linkage>
    $<$<COMPILE_LANGUAGE:C>:
    -Wno-unused-parameter>
    )
endif()

target_compile_features(oblib_base INTERFACE cxx_std_11)

if(OB_HAVE_BUNDLED_JEMALLOC)
  target_compile_definitions(oblib_base_without_pass
    INTERFACE OB_HAVE_BUNDLED_JEMALLOC=1)
endif()

set(_LIB_PATH "${DEP_DIR}/lib64/")
set(LIBAIO_LINK_OPTION "-laio")
if(APPLE OR ANDROID)
  set(_LIB_PATH "${DEP_DIR}/lib/")
  # macOS and Android don't have libaio
  set(LIBAIO_LINK_OPTION "")
  # On macOS, s2/protoc/protobuf/lz4/zstd are in lib64/
  if(APPLE)
    set(_LIB_PATH_EXTRA "${DEP_DIR}/lib64/")
  else()
    set(_LIB_PATH_EXTRA "${_LIB_PATH}")
  endif()
else()
  set(_LIB_PATH_EXTRA "${_LIB_PATH}")
endif()
set(OBLIB_ABSEIL_LIST   
    ${_LIB_PATH}/libabsl_base.a
    ${_LIB_PATH}/libabsl_city.a
    ${_LIB_PATH}/libabsl_debugging_internal.a
    ${_LIB_PATH}/libabsl_demangle_internal.a
    ${_LIB_PATH}/libabsl_hash.a
    ${_LIB_PATH}/libabsl_int128.a
    ${_LIB_PATH}/libabsl_low_level_hash.a
    ${_LIB_PATH}/libabsl_malloc_internal.a
    ${_LIB_PATH}/libabsl_raw_hash_set.a
    ${_LIB_PATH}/libabsl_raw_logging_internal.a
    ${_LIB_PATH}/libabsl_spinlock_wait.a
    ${_LIB_PATH}/libabsl_stacktrace.a
    ${_LIB_PATH}/libabsl_str_format_internal.a
    ${_LIB_PATH}/libabsl_strings.a
    ${_LIB_PATH}/libabsl_strings_internal.a
    ${_LIB_PATH}/libabsl_symbolize.a
    ${_LIB_PATH}/libabsl_synchronization.a
    ${_LIB_PATH}/libabsl_throw_delegate.a
    ${_LIB_PATH}/libabsl_time.a
    ${_LIB_PATH}/libabsl_time_zone.a)

# S2, Abseil and protobuf-c are pulled by the legacy GIS implementation only.
# Keep them out of the lightweight core link; the GIS package owns these
# dependencies when SEEKDB_ENABLE_CORE_GIS is disabled.
set(OBLIB_GIS_LINK_LIBS)
if (SEEKDB_ENABLE_CORE_GIS)
  if (WIN32)
    list(APPEND OBLIB_GIS_LINK_LIBS
      ${OB_VCPKG_DIR}/lib/s2.lib
      ${OB_VCPKG_DIR}/lib/abseil_dll.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_internal.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_reflection.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_marshalling.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_config.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_program_name.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_private_handle_accessor.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_commandlineflag.lib
      ${OB_VCPKG_DIR}/lib/absl_flags_commandlineflag_internal.lib
      ${OB_VCPKG_DIR}/lib/protobuf-c.lib)
  else()
    list(APPEND OBLIB_GIS_LINK_LIBS
      ${_LIB_PATH_EXTRA}/libs2.a
      ${OBLIB_ABSEIL_LIST}
      ${DEP_DIR}/lib/libprotobuf-c.a)
  endif()
endif()

# Check if system is Ubuntu >= 24 for dynamic linking libaio.so.1t64
if(OB_AIO AND OB_AIO STREQUAL "libaio1t64" AND NOT APPLE)
  set(AIO_SEARCH_PATHS)
  if(ARCHITECTURE STREQUAL "x86_64")
    list(APPEND AIO_SEARCH_PATHS /usr/lib/x86_64-linux-gnu)
  elseif(ARCHITECTURE STREQUAL "aarch64")
    list(APPEND AIO_SEARCH_PATHS /usr/lib/aarch64-linux-gnu)
  endif()
  list(APPEND AIO_SEARCH_PATHS /usr/lib)
  find_library(AIO_LIBRARY
    NAMES libaio.so.1t64
    PATHS ${AIO_SEARCH_PATHS}
    NO_DEFAULT_PATH
  )
  if(AIO_LIBRARY)
    set(LIBAIO_LINK_OPTION "${AIO_LIBRARY}")
    message(STATUS "Ubuntu ${DEBIAN_VERSION} detected, using ${OB_AIO} package and linking to ${AIO_LIBRARY}")
  else()
    # Fallback to lib: format if library not found at expected paths
    # This lets the linker search for libaio.so.1t64 in system library paths
    set(LIBAIO_LINK_OPTION "lib:libaio.so.1t64")
    message(STATUS "Ubuntu ${DEBIAN_VERSION} detected, using ${OB_AIO} package and lib:libaio.so.1t64")
  endif()
endif()

if(WIN32)
  set(VCPKG_LIB_DIR "${OB_VCPKG_DIR}/lib")
  set(OPENSSL_LIB_DIR "${OB_OPENSSL_DIR}/lib/VC/x64/MD")
  target_link_libraries(oblib_base_without_pass
    INTERFACE
    ${VCPKG_LIB_DIR}/libcurl.lib
    ${OPENSSL_LIB_DIR}/libssl.lib
    ${OPENSSL_LIB_DIR}/libcrypto.lib
    ${OBLIB_GIS_LINK_LIBS}
    ${VCPKG_LIB_DIR}/roaring.lib
    ${VCPKG_LIB_DIR}/zlib.lib
    ${VCPKG_LIB_DIR}/icuin.lib
    ${VCPKG_LIB_DIR}/icudt.lib
    ${VCPKG_LIB_DIR}/icuuc.lib
    ${VCPKG_LIB_DIR}/sqlite3.lib
    ${VCPKG_LIB_DIR}/pthreadVC3.lib
    ws2_32 crypt32 bcrypt userenv wldap32 advapi32
    ${ARCH_LDFLAGS}
  )
else()
  target_link_libraries(oblib_base_without_pass
    INTERFACE
    -L${DEP_DIR}/lib
    ${DEP_DIR}/lib/libcurl.a
    ${DEP_DIR}/lib/libssl.a
    ${DEP_DIR}/lib/libcrypto.a
    ${DEP_DIR}/lib/libroaring.a
    $<$<STREQUAL:"${ARCHITECTURE}","x86_64">:${DEP_DIR}/lib/libunwind.a>
    ${DEP_DIR}/lib/sqlite/libsqlite3.a
    ${DEP_DIR}/lib/libz.a
    ${DEP_DIR}/lib/libicui18n.a
    ${DEP_DIR}/lib/libicustubdata.a
    ${DEP_DIR}/lib/libicuuc.a
    ${OBLIB_GIS_LINK_LIBS}
    $<$<PLATFORM_ID:Linux>:-L${DEP_DIR}/var/usr/lib64 -L${DEP_DIR}/var/usr/lib -L${DEP_3RD_DIR}/usr/lib -L${DEP_3RD_DIR}/usr/lib64>
    ${LIBAIO_LINK_OPTION} $<$<NOT:$<BOOL:${ANDROID}>>:-lpthread> -ldl
    $<$<BOOL:${ANDROID}>:-llog>
    $<$<PLATFORM_ID:Linux>:-lrt>
    $<$<PLATFORM_ID:Darwin>:-Wl,-framework,Security -Wl,-framework,CoreFoundation -Wl,-framework,SystemConfiguration -liconv>
    ${ARCH_LDFLAGS}
  )
endif()
target_link_libraries(oblib_base INTERFACE oblib_base_without_pass oblib_pass)

function(oblib_add_library target)
  message(STATUS "oblib_add_library ${target}")
  add_library(${target} OBJECT ${ARGN})
  list(APPEND oblib_object_libraries ${target})
  set(oblib_object_libraries
    "${oblib_object_libraries}" CACHE INTERNAL oblib_object_libraries)
endfunction()

function(oblib_add_extra_objects)
  list(APPEND oblib_extra_objects ${ARGN})
  set(oblib_extra_objects "${oblib_extra_objects}" CACHE INTERNAL oblib_extra_objects)
endfunction()

set(oblib_object_libraries "" CACHE INTERNAL oblib_object_libraries)
set(oblib_extra_objects "" CACHE INTERNAL oblib_extra_objects)

add_subdirectory(lib)
add_subdirectory(common)
add_subdirectory(rpc)

# Define static library which contains previous object libraries.
message(STATUS "oblib_extra_objects ${oblib_extra_objects}")
set_source_files_properties(${oblib_extra_objects} PROPERTIES GENERATED TRUE)
add_library(oblib STATIC ${oblib_extra_objects})
target_link_libraries(oblib PUBLIC oblib_base ${oblib_object_libraries})
