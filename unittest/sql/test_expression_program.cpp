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
#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>
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

TEST(ExpressionProgramBridge, SharedPreparedHasIndependentConcurrentRunners)
{
  TestHost plan_host;
  constexpr size_t WORKERS = 4;
  constexpr int REBINDS = 64;
  struct WorkerResult {
    bool outputs_match = true;
    int completed = 0;
    int allocations = 0;
    int releases = 0;
    int polls = 0;
  };
  std::array<WorkerResult, WORKERS> results;
  {
    std::vector<Instruction> nodes(7);
    nodes[0].opcode = 0;
    nodes[0].input = 0;
    nodes[1].opcode = 0;
    nodes[1].input = 1;
    nodes[2].opcode = 2;
    nodes[2].argument_count = 2;
    nodes[3].opcode = 1;
    nodes[3].value = 0;
    nodes[4].opcode = 10;
    nodes[4].argument_start = 2;
    nodes[4].argument_count = 2;
    nodes[5].opcode = 18;
    nodes[5].argument_start = 4;
    nodes[5].argument_count = 3;
    nodes[6].opcode = 4;
    nodes[6].argument_start = 7;
    nodes[6].argument_count = 2;
    auto prepared = prepare(view(nodes), view(std::vector<uint32_t>{0, 1, 2, 3, 4, 2, 3, 2, 2}),
                            view(std::vector<uint32_t>{4}), view(std::vector<uint32_t>{5, 6}),
                            2, &plan_host);
    const Prepared &shared = *prepared;
    std::atomic<size_t> ready{0};
    std::atomic<bool> start{false};
    std::array<std::thread, WORKERS> workers;
    for (size_t worker = 0; worker < WORKERS; ++worker) {
      workers[worker] = std::thread([&shared, &ready, &start, &results, worker]() {
        TestHost host;
        bool announced = false;
        WorkerResult &result = results[worker];
        try {
          {
            constexpr size_t CAPACITY = 65;
            auto runner = make_runner(shared, &host, CAPACITY);
            ready.fetch_add(1, std::memory_order_release);
            announced = true;
            while (!start.load(std::memory_order_acquire)) {
              std::this_thread::yield();
            }
            for (int rebound = 0; rebound < REBINDS; ++rebound) {
              auto input = input_storage(*runner);
              const size_t rows = rebound % 2 == 0 ? CAPACITY : 3;
              for (size_t row = 0; row < rows; ++row) {
                input[row] = row % 3 == 0 ? worker * 100 + rebound + 1
                    : static_cast<uint64_t>(-1000);
                input[CAPACITY + row] = rebound * 2;
                input[2 * CAPACITY + row] = row % 3 != 2;
                input[3 * CAPACITY + row] = 1;
              }
              const std::array<uint8_t, CAPACITY> skip{};
              const auto status = execute(*runner, shared, rows,
                  rust::Slice<const uint8_t>(skip.data(), rows));
              if (status.code != 0) {
                result.outputs_match = false;
                break;
              }
              const auto selected = selection(*runner);
              const auto first = node_values(*runner, 5);
              const auto second = node_values(*runner, 6);
              const auto first_flags = node_flags(*runner, 5);
              const auto second_flags = node_flags(*runner, 6);
              const uint64_t expected = worker * 100 + rebound * 3 + 1;
              for (size_t row = 0; row < rows; ++row) {
                const bool survives = row % 3 == 0;
                result.outputs_match &= selected[row] == (survives ? 0 : 1)
                    && first_flags[row] == (survives ? 3 : 0)
                    && second_flags[row] == (survives ? 3 : 0);
                if (survives) {
                  result.outputs_match &= first[row] == expected && second[row] == expected * expected;
                }
              }
              ++result.completed;
            }
          }
        } catch (...) {
          result.outputs_match = false;
          if (!announced) {
            ready.fetch_add(1, std::memory_order_release);
          }
        }
        result.allocations = host.allocations_;
        result.releases = host.releases_;
        result.polls = host.polls_;
      });
    }
    while (ready.load(std::memory_order_acquire) != WORKERS) {
      std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto &worker : workers) {
      worker.join();
    }
    EXPECT_EQ(1, plan_host.allocations_);
    EXPECT_EQ(0, plan_host.releases_);
    EXPECT_EQ(0, plan_host.polls_);
    for (size_t worker = 0; worker < WORKERS; ++worker) {
      SCOPED_TRACE(worker);
      EXPECT_TRUE(results[worker].outputs_match);
      EXPECT_EQ(REBINDS, results[worker].completed);
      EXPECT_EQ(1, results[worker].allocations);
      EXPECT_EQ(1, results[worker].releases);
      EXPECT_EQ(REBINDS, results[worker].polls);
    }
  }
  EXPECT_EQ(1, plan_host.releases_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
