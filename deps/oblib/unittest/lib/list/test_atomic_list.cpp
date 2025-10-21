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

#include <pthread.h>
#include "lib/allocator/ob_malloc.h"
#include "lib/list/ob_atomic_list.h"

using namespace oceanbase;
using namespace common;

ObAtomicList atomic_list;

void test_simple()
{
  char mem[32];
  char *next = mem + 16;

  atomic_list.init("test", 0);
  OB_ASSERT(atomic_list.empty());
  OB_ASSERT(NULL == atomic_list.head());
  OB_ASSERT(TO_PTR(atomic_list.head()) == atomic_list.push(mem));
  OB_ASSERT(!atomic_list.empty());
  OB_ASSERT(mem == atomic_list.head());
  OB_ASSERT(mem == atomic_list.pop());
  OB_ASSERT(atomic_list.empty());

  OB_ASSERT(TO_PTR(atomic_list.head()) == atomic_list.push(mem));
  OB_ASSERT(TO_PTR(atomic_list.head()) == atomic_list.push(next));
  OB_ASSERT(!atomic_list.empty());
  OB_ASSERT(next == atomic_list.head());
  OB_ASSERT(mem == atomic_list.next(next));
  OB_ASSERT(next == atomic_list.pop());
  OB_ASSERT(mem == atomic_list.pop());
}

void *thread_func(void *arg)
{
  char *mem = (char *)ob_malloc(64 * 1024, 0);
  OB_ASSERT(NULL != mem);
  int64_t index = *(int64_t *)arg;

  void *freelist = NULL;
  void *ptr = NULL;
  int64_t i = 0;

  for (i = 0; i < 64 * 1024 / 16; i++) {
    atomic_list.push(mem + i * 16);
  }

  for (int64_t j = 0; j < 100000; j++) {
    for (i = 0; i < 100; i++) {
      ptr = atomic_list.pop();
      OB_ASSERT(NULL != ptr);
      *(reinterpret_cast<void**>(ptr)) = freelist;
      freelist = ptr;
    }

    for (i = 0; i < 100; i++) {
      ptr = freelist;
      freelist = *(void **)ptr;
      OB_ASSERT(NULL != atomic_list.push(ptr));
    }
    if (0 == (j + 1) % 100000 && j > 0) {
      printf("thread %ld pop %ld times and push %ld times\n",
             index, 1000000L, 1000000L);
    }
  }

  return NULL;
}

void thread_run()
{
  pthread_t id[15];
  int64_t index[15];

  printf("start multi-thread concurrency test\n");
  for (int64_t i = 0; i < 15; i++) {
    index[i] = i;
    if (0 != pthread_create(&id[i], NULL, thread_func, &index[i])) {
      printf("create thread error\n");
    }
  }

  void *ret = NULL;
  for (int64_t i = 0; i < 15; i++) {
    pthread_join(id[i], &ret);
  }

  int64_t count = 0;
  for (int64_t i = 0; i < 15 * 64 * 1024 / 16; i++) {
    OB_ASSERT(NULL != atomic_list.pop());
    count++;
  }
  OB_ASSERT(count == 15 * 64 * 1024 / 16);

  printf("end test\n");
}

int main(int argc, char **argv)
{
  UNUSED(argc);
  UNUSED(argv);

  test_simple();

  thread_run();

  return 0;
}
