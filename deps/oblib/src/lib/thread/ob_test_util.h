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

#ifndef OCEANBASE_COMMON_TEST_UTIL_
#define OCEANBASE_COMMON_TEST_UTIL_

#include "lib/thread/thread_pool.h"
/*
 * for testing code clips in multithread env. sample:
 *========================================
 * BEGIN_THREAD_CODE(MyClass, 10)
 * {
 *     static int i = 0;
 *     ASSERT_TRUE(__sync_fetch_and_add(i) > 0);
 * }
 * END_THREAD_CODE(MyClass);
 *=========================================
 */
#define BEGIN_THREAD_CODE(class_name, thread_count)             \
  class _##class_name : public oceanbase::lib::ThreadPool       \
  {                                                             \
public:                                                         \
_##class_name() { set_thread_count(thread_count); }             \
void run1() final {                                             \
                                                                \

#define END_THREAD_CODE(class_name) \
  }};\
  _##class_name my_##class_name; \
  my_##class_name.start();  my_##class_name.wait();

#define OK(ass) ASSERT_EQ(OB_SUCCESS, (ass))
#define BAD(ass) ASSERT_NE(OB_SUCCESS, (ass))

#endif
