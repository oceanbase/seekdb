// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// The exact same object must link with new Rust and fail to link with old Rust.
#include "nio.h"
#include <cstdio>
static_assert(NIO_ABI_VERSION == 27, "This caller must use the new header");
int main()
{
  int32_t error = -1;
  nio_reactor *reactor = nio_start_v27(nullptr, NIO_ABI_VERSION, nullptr, 0, 0, 0,
      nullptr, 0, &error, 1, nullptr, 0);
  if (reactor) { nio_stop(reactor); nio_wait_destroy(reactor); return 1; }
  if (error != NIO_START_EINVAL) return 1;
  std::puts("NIO_ABI27_CALLER_PASS");
  return 0;
}
