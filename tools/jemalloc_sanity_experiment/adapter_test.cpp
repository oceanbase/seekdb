#include "lib/allocator/ob_jemalloc.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

extern int64_t sanity_min_addr;
extern int64_t sanity_max_addr;

using oceanbase::common::jemalloc_sanity_free;
using oceanbase::common::jemalloc_sanity_malloc;

__attribute__((noinline)) int run_case(const char *mode, char *ptr) {
  if (0 == std::strcmp(mode, "overflow")) {
    ptr[13] = 'x';
  } else if (0 == std::strcmp(mode, "uaf")) {
    jemalloc_sanity_free(ptr);
    ptr[0] = 'x';
    return 0;
  } else if (0 == std::strcmp(mode, "memcpy_overflow")) {
    char source[14] = {};
    std::memcpy(ptr, source, sizeof(source));
  } else if (0 == std::strcmp(mode, "snprintf_overflow")) {
    std::snprintf(ptr, 32, "%s", "formatted output");
  } else if (0 == std::strcmp(mode, "sprintf_overflow")) {
    std::sprintf(ptr, "%s", "formatted output");
  } else {
    std::memset(ptr, 0x5a, 13);
    if (0x5a != ptr[12]) {
      return 3;
    }
  }
  jemalloc_sanity_free(ptr);
  return 0;
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "valid";
  char *ptr = static_cast<char *>(jemalloc_sanity_malloc(13));
  if (nullptr == ptr) {
    std::fprintf(stderr, "allocation failed\n");
    return 2;
  }

  std::printf("sanity range: [0x%llx, 0x%llx), %.2f TiB\n",
              static_cast<unsigned long long>(sanity_min_addr),
              static_cast<unsigned long long>(sanity_max_addr),
              static_cast<double>(sanity_max_addr - sanity_min_addr) /
                  static_cast<double>(1ULL << 40));
  std::fflush(stdout);

  return run_case(mode, ptr);
}
