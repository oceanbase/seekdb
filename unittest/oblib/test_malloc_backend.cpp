/*
 * Copyright (c) 2025 OceanBase.
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
#include <gtest/gtest.h>
#include "lib/allocator/ob_malloc.h"
#if defined(OB_HAVE_BUNDLED_JEMALLOC) && !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace oceanbase::common;

TEST(TestMallocBackend, parse)
{
  ASSERT_STREQ("MALLOC_BACKEND", ob_malloc_backend_env_name());
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  ASSERT_EQ(OB_MALLOC_BACKEND_JEMALLOC, parse_ob_malloc_backend(NULL));
  ASSERT_EQ(OB_MALLOC_BACKEND_JEMALLOC, parse_ob_malloc_backend(""));
#else
  ASSERT_EQ(OB_MALLOC_BACKEND_OBMALLOC, parse_ob_malloc_backend(NULL));
  ASSERT_EQ(OB_MALLOC_BACKEND_OBMALLOC, parse_ob_malloc_backend(""));
#endif
  ASSERT_EQ(OB_MALLOC_BACKEND_OBMALLOC, parse_ob_malloc_backend("obmalloc"));
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  ASSERT_EQ(OB_MALLOC_BACKEND_JEMALLOC, parse_ob_malloc_backend("jemalloc"));
#else
  ASSERT_EQ(OB_MALLOC_BACKEND_UNKNOWN, parse_ob_malloc_backend("jemalloc"));
#endif
  ASSERT_EQ(OB_MALLOC_BACKEND_UNKNOWN, parse_ob_malloc_backend("system"));
  ASSERT_EQ(OB_MALLOC_BACKEND_UNKNOWN, parse_ob_malloc_backend("glibc"));
  ASSERT_EQ(OB_MALLOC_BACKEND_UNKNOWN, parse_ob_malloc_backend("mimalloc"));
  ASSERT_EQ(OB_MALLOC_BACKEND_UNKNOWN, parse_ob_malloc_backend("other"));
  ASSERT_EQ(OB_MALLOC_BACKEND_UNKNOWN, parse_ob_malloc_backend("invalid"));

  ASSERT_TRUE(is_ob_malloc_backend(OB_MALLOC_BACKEND_OBMALLOC));
  ASSERT_FALSE(is_ob_malloc_backend(OB_MALLOC_BACKEND_JEMALLOC));
  ASSERT_FALSE(is_jemalloc_backend(OB_MALLOC_BACKEND_OBMALLOC));
  ASSERT_TRUE(is_jemalloc_backend(OB_MALLOC_BACKEND_JEMALLOC));
  ASSERT_FALSE(is_jemalloc_backend(OB_MALLOC_BACKEND_UNKNOWN));
}

TEST(TestMallocBackend, detect_once)
{
  const ObMallocBackend first = get_ob_malloc_backend();
  ASSERT_EQ(0, setenv(ob_malloc_backend_env_name(),
                      OB_MALLOC_BACKEND_OBMALLOC == first ? "jemalloc" : "obmalloc", 1));
  ASSERT_EQ(first, get_ob_malloc_backend());
}

#if defined(OB_HAVE_BUNDLED_JEMALLOC)
TEST(TestMallocBackend, direct_jemalloc)
{
  ASSERT_TRUE(is_jemalloc_backend());

  ObMemAttr attr;
  void *ptr = ob_malloc(100, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(ob_malloc_usable_size(ptr), 100);

  ptr = ob_realloc(ptr, 200, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(ob_malloc_usable_size(ptr), 200);
  ob_free(ptr);

  ptr = jemalloc_memalign(64, 100);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 64);
  ob_free(ptr);
}

#if !defined(_WIN32)
TEST(TestMallocBackend, restore_after_fork)
{
  bool enabled = false;
  size_t enabled_size = sizeof(enabled);
  ASSERT_EQ(0, je_mallctl("background_thread", &enabled, &enabled_size,
                          nullptr, 0));
  ASSERT_TRUE(enabled);

  const pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (0 == pid) {
    enabled = true;
    enabled_size = sizeof(enabled);
    if (0 != je_mallctl("background_thread", &enabled, &enabled_size,
                        nullptr, 0) || enabled) {
      _exit(1);
    } else if (!restore_malloc_backend_after_fork()) {
      _exit(2);
    }
    enabled = false;
    enabled_size = sizeof(enabled);
    if (0 != je_mallctl("background_thread", &enabled, &enabled_size,
                        nullptr, 0) || !enabled) {
      _exit(3);
    }
    _exit(0);
  }

  int status = 0;
  ASSERT_EQ(pid, waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}
#endif
#endif

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
