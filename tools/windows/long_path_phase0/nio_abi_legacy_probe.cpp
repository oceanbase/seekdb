// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// This same object is linked once to ABI 26 and once to ABI 27 Rust.
#include "nio.h"
#include <cstdio>
#include <cstdlib>
static_assert(NIO_ABI_VERSION == 26, "This caller must use the pinned old header");
int main(int argc, char **argv)
{
  if (argc != 2) return 2;
  const int expected = std::atoi(argv[1]);
  int32_t error = -1;
  nio_reactor *reactor = nio_start(nullptr, NIO_ABI_VERSION, nullptr, 0, 0, 0,
      nullptr, 0, &error, 1);
  if (reactor) { nio_stop(reactor); nio_wait_destroy(reactor); return 1; }
  if (error != expected) { std::fprintf(stderr, "ABI26 caller error=%d expected=%d\n", error, expected); return 1; }
  std::printf("NIO_ABI26_CALLER_PASS error=%d\n", error);
  return 0;
}
