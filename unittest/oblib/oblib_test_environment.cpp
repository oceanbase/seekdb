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

#include <csignal>

#include "gtest/gtest.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/alloc/ob_malloc_allocator.h"
#include "unittest/oblib/oblib_test_environment.h"

namespace oceanbase
{
namespace oblib_test
{

bool has_unfree = false;

class OBLibTestEnvironment : public ::testing::Environment
{
public:
  void SetUp() override
  {
    std::signal(49, SIG_IGN);
    lib::enable_memleak_light_backtrace(true);
  }
};

::testing::Environment *const OBLIB_TEST_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new OBLibTestEnvironment());

} // namespace oblib_test
} // namespace oceanbase

void has_unfree_callback(char *)
{
  oceanbase::oblib_test::has_unfree = true;
}
