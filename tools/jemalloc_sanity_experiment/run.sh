#!/usr/bin/env bash

set -euo pipefail

TOPDIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUTDIR="${TOPDIR}/build_bazel/jemalloc_sanity_experiment"
DEPS="${TOPDIR}/deps/3rd/usr/local/oceanbase"
CLANG="${DEPS}/devtools/bin/clang++"
COMMON_FLAGS=(
  -std=c++17 -g -O1 -Wno-inconsistent-missing-override
  -DENABLE_SANITY -DOB_HAVE_BUNDLED_JEMALLOC=1
  -I"${TOPDIR}/src/oblib"
  -I"${DEPS}/deps/devel/include"
  -I"${DEPS}/devtools/include"
)
NO_BUILTIN_FLAGS=(
  -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset
  -fno-builtin-bzero -fno-builtin-memcmp
  -fno-builtin-strlen -fno-builtin-strnlen
  -fno-builtin-strcpy -fno-builtin-strncpy
  -fno-builtin-strcmp -fno-builtin-strncmp
  -fno-builtin-strcasecmp -fno-builtin-strncasecmp
  -fno-builtin-vsprintf -fno-builtin-vsnprintf
  -fno-builtin-sprintf -fno-builtin-snprintf
)
WRAP_FLAGS=(
  -Wl,--wrap=memcpy -Wl,--wrap=memmove -Wl,--wrap=memset
  -Wl,--wrap=bzero -Wl,--wrap=memcmp -Wl,--wrap=strlen
  -Wl,--wrap=strnlen -Wl,--wrap=strcpy -Wl,--wrap=strncpy
  -Wl,--wrap=strcmp -Wl,--wrap=strncmp
  -Wl,--wrap=strcasecmp -Wl,--wrap=strncasecmp
  -Wl,--wrap=vsprintf -Wl,--wrap=vsnprintf
  -Wl,--wrap=sprintf -Wl,--wrap=snprintf
)

mkdir -p "${OUTDIR}"
"${CLANG}" "${COMMON_FLAGS[@]}" -c \
  "${TOPDIR}/src/oblib/lib/allocator/ob_jemalloc_sanity.cpp" \
  -o "${OUTDIR}/adapter.o"
"${CLANG}" "${COMMON_FLAGS[@]}" -c \
  "${TOPDIR}/src/oblib/lib/allocator/ob_sanity_libc_wrap.cpp" \
  -o "${OUTDIR}/libc_wrap.o"
"${CLANG}" "${COMMON_FLAGS[@]}" \
  -fpass-plugin="${DEPS}/devtools/lib64/libsanitypass.so" \
  "${NO_BUILTIN_FLAGS[@]}" \
  -c "${TOPDIR}/tools/jemalloc_sanity_experiment/adapter_test.cpp" \
  -o "${OUTDIR}/adapter_test.o"
"${CLANG}" "${OUTDIR}/adapter_test.o" "${OUTDIR}/adapter.o" \
  "${OUTDIR}/libc_wrap.o" "${DEPS}/deps/devel/lib/libjemalloc_pic.a" \
  "${WRAP_FLAGS[@]}" -no-pie -pthread -ldl -o "${OUTDIR}/adapter_test"

MODE="${1:-valid}"
if [[ "${MODE}" == arena_* ]]; then
  "${CLANG}" "${COMMON_FLAGS[@]}" \
    -fpass-plugin="${DEPS}/devtools/lib64/libsanitypass.so" \
    "${NO_BUILTIN_FLAGS[@]}" \
    -c "${TOPDIR}/tools/jemalloc_sanity_experiment/page_arena_test.cpp" \
    -o "${OUTDIR}/page_arena_test.o"
  "${CLANG}" "${OUTDIR}/page_arena_test.o" "${OUTDIR}/adapter.o" \
    "${OUTDIR}/libc_wrap.o" "${DEPS}/deps/devel/lib/libjemalloc_pic.a" \
    "${WRAP_FLAGS[@]}" -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all \
    -no-pie -pthread -ldl \
    -o "${OUTDIR}/page_arena_test"
  "${OUTDIR}/page_arena_test" "${MODE}"
else
  "${OUTDIR}/adapter_test" "${MODE}"
fi
