// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>
#include "rust/expression_program.h"
#include "rust/cxx.h"

using namespace oceanbase::expression;

namespace {
class TestHost final : public IExecutionHost
{
public:
  uint64_t *allocate(size_t words) override
  {
    if (fail_allocation_) { return nullptr; }
    auto *memory = static_cast<uint64_t *>(std::calloc(words, sizeof(uint64_t)));
    if (memory != nullptr) { ++allocations_; }
    return memory;
  }
  void release(uint64_t *data, size_t) override { ++releases_; std::free(data); }
  int32_t check_cancel() override { ++polls_; return cancellation_; }
  int allocations_ = 0;
  int releases_ = 0;
  int polls_ = 0;
  int cancellation_ = 0;
  bool fail_allocation_ = false;
};

template <typename T>
rust::Slice<const T> view(const std::vector<T> &values)
{
  return {values.data(), values.size()};
}

rust::Box<Prepared> prepare_add(TestHost &host)
{
  std::vector<Instruction> nodes(3);
  nodes[0].opcode = 0;
  nodes[0].input = 0;
  nodes[1].opcode = 1;
  nodes[1].value = 7;
  nodes[2].opcode = 2;
  nodes[2].argument_count = 2;
  return prepare(view(nodes), view(std::vector<uint32_t>{0, 1}),
                 view(std::vector<uint32_t>{}), view(std::vector<uint32_t>{2}), 1, &host);
}
}

TEST(ExpressionProgramBridge, RebindNullSkipAndResultLifetime)
{
  TestHost host;
  std::vector<int64_t> consumed;
  {
    auto prepared = prepare_add(host);
    auto runner = make_runner(*prepared, &host, 3);
    auto input = input_storage(*runner);
    auto *values = input.data();
    values[0] = 4;
    values[1] = 5;
    values[2] = 6;
    auto *validity = input.data() + 3;
    validity[0] = 1;
    validity[1] = 0;
    validity[2] = 1;
    ASSERT_EQ(0, execute(*runner, *prepared, 3, view(std::vector<uint8_t>{0, 0, 1})).code);
    const auto outputs = node_values(*runner, 2);
    const auto flags = node_flags(*runner, 2);
    ASSERT_EQ(3U, flags[0]);
    ASSERT_EQ(1U, flags[1]);
    ASSERT_EQ(0U, flags[2]);
    consumed.push_back(static_cast<int64_t>(outputs[0]));
    input_storage(*runner)[0] = static_cast<uint64_t>(-10);
    input_storage(*runner)[3] = 1;
    ASSERT_EQ(0, execute(*runner, *prepared, 1, view(std::vector<uint8_t>{0})).code);
    ASSERT_EQ(-3, static_cast<int64_t>(node_values(*runner, 2)[0]));
    ASSERT_EQ(11, consumed[0]);
  }
  ASSERT_EQ(2, host.allocations_);
  ASSERT_EQ(2, host.releases_);
  ASSERT_EQ(11, consumed[0]);
}

TEST(ExpressionProgramBridge, AllocationFailureAndCancellation)
{
  TestHost failing;
  failing.fail_allocation_ = true;
  EXPECT_THROW(prepare_add(failing), rust::Error);
  EXPECT_EQ(0, failing.allocations_);
  EXPECT_EQ(0, failing.releases_);
  {
    failing.fail_allocation_ = false;
    auto prepared = prepare_add(failing);
    failing.fail_allocation_ = true;
    EXPECT_THROW(make_runner(*prepared, &failing, 1), rust::Error);
    EXPECT_EQ(1, failing.allocations_);
    EXPECT_EQ(0, failing.releases_);
  }
  EXPECT_EQ(1, failing.releases_);
  TestHost host;
  host.cancellation_ = -4012;
  {
    auto prepared = prepare_add(host);
    auto runner = make_runner(*prepared, &host, 1);
    input_storage(*runner)[1] = 1;
    EXPECT_EQ(-4012, execute(*runner, *prepared, 1, view(std::vector<uint8_t>{0})).code);
    EXPECT_EQ(1, host.polls_);
  }
  EXPECT_EQ(2, host.releases_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
