/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <malloc.h>
#include <gtest/gtest.h>
#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_malloc.h"

using namespace oceanbase::common;

TEST(TestJemallocHook, CrossApiAllocationDomain)
{
  static const size_t SIZE = 128;
  void *ptr = malloc(SIZE);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(malloc_usable_size(ptr), jemalloc_usable_size(ptr));
  memset(ptr, 0x5a, SIZE);
  jemalloc_free(ptr);

  ptr = jemalloc_malloc(SIZE);
  ASSERT_NE(nullptr, ptr);
  memset(ptr, 0xa5, SIZE);
  free(ptr);
}

TEST(TestJemallocHook, ReallocAndAlignment)
{
  static const size_t SIZE = 128;
  char expected[SIZE];
  memset(expected, 0x3c, sizeof(expected));

  void *ptr = malloc(SIZE);
  ASSERT_NE(nullptr, ptr);
  memcpy(ptr, expected, SIZE);
  ptr = realloc(ptr, SIZE * 2);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0, memcmp(ptr, expected, SIZE));
  ASSERT_GE(malloc_usable_size(ptr), SIZE * 2);
  free(ptr);

  static const size_t ALIGNMENT = 4096;
  ptr = memalign(ALIGNMENT, SIZE);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0, reinterpret_cast<uintptr_t>(ptr) & (ALIGNMENT - 1));
  free(ptr);
}
