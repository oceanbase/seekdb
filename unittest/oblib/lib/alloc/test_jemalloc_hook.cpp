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
#if defined(__linux__) || defined(_WIN32)
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#endif
#include <new>
#include <gtest/gtest.h>
#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_malloc.h"

using namespace oceanbase::common;

#if defined(__linux__)
TEST(TestJemallocHook, CrossApiAllocationDomain)
{
  static const size_t SIZE = 128;
  void *ptr = malloc(SIZE);
  ASSERT_NE(nullptr, ptr);
#if defined(OB_USE_ASAN)
  ASSERT_GE(malloc_usable_size(ptr), SIZE);
  memset(ptr, 0x5a, SIZE);
  free(ptr);
#else
  ASSERT_EQ(malloc_usable_size(ptr), jemalloc_usable_size(ptr));
  memset(ptr, 0x5a, SIZE);
  jemalloc_free(ptr);

  ptr = jemalloc_malloc(SIZE);
  ASSERT_NE(nullptr, ptr);
  memset(ptr, 0xa5, SIZE);
  free(ptr);
#endif
}

TEST(TestJemallocHook, EntireUsableRangeIsAccessible)
{
  void *ptr = malloc(13);
  ASSERT_NE(nullptr, ptr);
  const size_t usable = malloc_usable_size(ptr);
  ASSERT_GE(usable, 13U);
  memset(ptr, 0x5A, usable);
  EXPECT_EQ(0x5A, static_cast<unsigned char *>(ptr)[usable - 1]);
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

TEST(TestMallocHookExtended, CAllocationEntryPoints)
{
  static const size_t SIZE = 97;
  void *ptr = calloc(4, SIZE);
  ASSERT_NE(nullptr, ptr);
  free(ptr);

  char *copy = strdup("seekdb-jmalloc");
  ASSERT_NE(nullptr, copy);
  ASSERT_STREQ("seekdb-jmalloc", copy);
  free(copy);

  copy = strndup("seekdb-jmalloc", 6);
  ASSERT_NE(nullptr, copy);
  ASSERT_STREQ("seekdb", copy);
  free(copy);

  ptr = aligned_alloc(64, 128);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 64);
  free(ptr);

  ASSERT_EQ(0, posix_memalign(&ptr, 128, SIZE));
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 128);
  free(ptr);

  ptr = valloc(SIZE);
  ASSERT_NE(nullptr, ptr);
  free(ptr);
  ptr = pvalloc(SIZE);
  ASSERT_NE(nullptr, ptr);
  free(ptr);

#if defined(__linux__)
  ptr = reallocarray(nullptr, 4, SIZE);
  ASSERT_NE(nullptr, ptr);
  free(ptr);
#endif
}

TEST(TestMallocHookExtended, CppAllocationEntryPoints)
{
  void *ptr = ::operator new(97);
  ASSERT_NE(nullptr, ptr);
  ::operator delete(ptr);

  ptr = ::operator new[](193, std::nothrow);
  ASSERT_NE(nullptr, ptr);
  ::operator delete[](ptr, std::nothrow);

  ptr = ::operator new(128, std::align_val_t(64));
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 64);
  ::operator delete(ptr, std::align_val_t(64));

  ptr = ::operator new[](256, std::align_val_t(128), std::nothrow);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 128);
  ::operator delete[](ptr, std::align_val_t(128), std::nothrow);
}
#elif defined(__APPLE__) && defined(OB_HAVE_BUNDLED_JEMALLOC)
TEST(TestJemallocHook, DarwinZoneAllocationDomain)
{
  ASSERT_TRUE(configure_darwin_malloc_zone());
  void *ptr = malloc(128);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(jemalloc_usable_size(ptr), malloc_size(ptr));
  jemalloc_free(ptr);
}
#else
TEST(TestJemallocHook, PlatformAllocatorSmoke)
{
  void *ptr = malloc(128);
  ASSERT_NE(nullptr, ptr);
  free(ptr);
}
#endif
