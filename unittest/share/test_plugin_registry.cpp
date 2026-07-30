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

#include "share/plugin/ob_plugin_registry.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

struct TestServiceV1
{
  uint32_t struct_size_;
  int (*value_)();
};

int value_one()
{
  return 1;
}

int value_two()
{
  return 2;
}

const TestServiceV1 SERVICE_ONE = {sizeof(TestServiceV1), value_one};
const TestServiceV1 SERVICE_TWO = {sizeof(TestServiceV1), value_two};

std::shared_ptr<ObPluginGeneration> make_initializing_generation(
    const char *plugin_id,
    const uint64_t generation)
{
  std::shared_ptr<ObPluginGeneration> owner(
      new ObPluginGeneration(plugin_id, generation));
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::VALIDATED));
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::LOADED));
  EXPECT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::INITIALIZING));
  return owner;
}

} // namespace

TEST(TestPluginRegistry, rejects_invalid_lifecycle_transition)
{
  ObPluginGeneration generation("com.seekdb.test", 1);
  EXPECT_EQ(OB_STATE_NOT_MATCH, generation.transition_to(ObPluginState::ACTIVE));
  EXPECT_EQ(ObPluginState::DISCOVERED, generation.state());
  EXPECT_EQ(OB_SUCCESS, generation.transition_to(ObPluginState::VALIDATED));
  EXPECT_EQ(OB_STATE_NOT_MATCH, generation.transition_to(ObPluginState::STOPPED));
}

TEST(TestPluginRegistry, blocked_generation_cannot_bypass_terminal_loader)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.blocked", 9);
  ASSERT_EQ(OB_SUCCESS, owner->transition_to(ObPluginState::BLOCKED));
  EXPECT_EQ(ObPluginState::BLOCKED, owner->state());
  EXPECT_EQ(OB_STATE_NOT_MATCH, owner->transition_to(ObPluginState::ACTIVE));
  EXPECT_EQ(OB_STATE_NOT_MATCH, registry.quiesce(owner));
  EXPECT_EQ(OB_STATE_NOT_MATCH, registry.mark_stopped(owner));
  EXPECT_EQ(ObPluginState::BLOCKED, owner->state());
}

TEST(TestPluginRegistry, registration_is_staged_and_versioned)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.example", 1);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.example.value", 1, 2, 3, 0x5, &SERVICE_ONE));

  ObPluginLease invisible;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 1, 0, invisible));
  ASSERT_EQ(OB_SUCCESS, registration.commit());
  EXPECT_EQ(ObPluginState::ACTIVE, owner->state());

  ObPluginLease compatible;
  ASSERT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.example.value", 1, 1, compatible));
  ASSERT_TRUE(compatible.is_valid());
  EXPECT_EQ(2U, compatible.service_minor());
  EXPECT_EQ(3U, compatible.service_patch());
  EXPECT_EQ(0x5U, compatible.service_capabilities());
  EXPECT_STREQ("com.seekdb.example", compatible.owner_plugin_id());
  EXPECT_EQ(1U, compatible.owner_generation());
  const auto *service = static_cast<const TestServiceV1 *>(compatible.service());
  ASSERT_NE(nullptr, service);
  EXPECT_EQ(1, service->value_());

  ObPluginLease too_new;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 1, 3, too_new));
  ObPluginLease patch_and_capability;
  EXPECT_EQ(OB_SUCCESS,
            registry.acquire("com.seekdb.example.value", 1, 2, 3, 0x1,
                             patch_and_capability));
  ObPluginLease missing_capability;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 1, 2, 3, 0x8,
                             missing_capability));
  ObPluginLease wrong_major;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.example.value", 2, 0, wrong_major));
}

TEST(TestPluginRegistry, duplicate_commit_has_no_partial_publication)
{
  ObPluginServiceRegistry registry;
  const auto first = make_initializing_generation("com.seekdb.first", 1);
  ObPluginRegistration first_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(first, first_registration));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_service("com.seekdb.shared.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, first_registration.commit());

  const auto second = make_initializing_generation("com.seekdb.second", 1);
  ObPluginRegistration second_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(second, second_registration));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service("com.seekdb.second.unique", 1, 0, &SERVICE_TWO));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service("com.seekdb.shared.value", 1, 1, &SERVICE_TWO));
  EXPECT_EQ(OB_ENTRY_EXIST, second_registration.commit());
  EXPECT_TRUE(second_registration.is_open());
  EXPECT_EQ(1, registry.service_count());

  ObPluginLease unique;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.second.unique", 1, 0, unique));
  second_registration.rollback();
  EXPECT_FALSE(second_registration.is_open());
}

TEST(TestPluginRegistry, quiesce_unpublishes_then_drains_leases)
{
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.drain", 7);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.drain.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, registration.commit());

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS, registry.acquire("com.seekdb.drain.value", 1, 0, lease));
  EXPECT_EQ(1, owner->lease_count());
  ASSERT_EQ(OB_SUCCESS, registry.quiesce(owner));
  EXPECT_EQ(ObPluginState::QUIESCING, owner->state());
  EXPECT_EQ(0, registry.service_count());

  ObPluginLease after_quiesce;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            registry.acquire("com.seekdb.drain.value", 1, 0, after_quiesce));
  EXPECT_EQ(OB_TIMEOUT, owner->wait_for_drain(0));
  EXPECT_EQ(OB_EAGAIN, registry.mark_stopped(owner));

  ObPluginLease moved(std::move(lease));
  EXPECT_FALSE(lease.is_valid());
  EXPECT_TRUE(moved.is_valid());
  moved.reset();
  EXPECT_EQ(OB_SUCCESS, owner->wait_for_drain(1000));
  EXPECT_EQ(OB_SUCCESS, registry.mark_stopped(owner));
  EXPECT_EQ(ObPluginState::STOPPED, owner->state());
}

TEST(TestPluginRegistry, new_generation_can_replace_drained_generation)
{
  ObPluginServiceRegistry registry;
  const auto first = make_initializing_generation("com.seekdb.upgrade", 1);
  ObPluginRegistration first_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(first, first_registration));
  ASSERT_EQ(OB_SUCCESS,
            first_registration.add_service("com.seekdb.upgrade.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, first_registration.commit());
  ASSERT_EQ(OB_SUCCESS, registry.quiesce(first));
  ASSERT_EQ(OB_SUCCESS, first->wait_for_drain(0));
  ASSERT_EQ(OB_SUCCESS, registry.mark_stopped(first));

  const auto second = make_initializing_generation("com.seekdb.upgrade", 2);
  ObPluginRegistration second_registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(second, second_registration));
  ASSERT_EQ(OB_SUCCESS,
            second_registration.add_service("com.seekdb.upgrade.value", 1, 1, &SERVICE_TWO));
  ASSERT_EQ(OB_SUCCESS, second_registration.commit());

  ObPluginLease lease;
  ASSERT_EQ(OB_SUCCESS, registry.acquire("com.seekdb.upgrade.value", 1, 0, lease));
  EXPECT_EQ(2U, lease.owner_generation());
  const auto *service = static_cast<const TestServiceV1 *>(lease.service());
  ASSERT_NE(nullptr, service);
  EXPECT_EQ(2, service->value_());
}

TEST(TestPluginRegistry, acquire_and_quiesce_are_linearizable)
{
  static const int THREAD_COUNT = 16;
  ObPluginServiceRegistry registry;
  const auto owner = make_initializing_generation("com.seekdb.concurrent", 1);
  ObPluginRegistration registration;
  ASSERT_EQ(OB_SUCCESS, registry.begin_registration(owner, registration));
  ASSERT_EQ(OB_SUCCESS,
            registration.add_service("com.seekdb.concurrent.value", 1, 0, &SERVICE_ONE));
  ASSERT_EQ(OB_SUCCESS, registration.commit());

  std::atomic<int> ready(0);
  std::atomic<int> attempted(0);
  std::atomic<int> acquired(0);
  std::atomic<int> missing(0);
  std::atomic<int> unexpected(0);
  std::atomic<bool> go(false);
  std::atomic<bool> release(false);
  std::vector<std::thread> threads;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() {
      ready.fetch_add(1);
      while (!go.load()) {
        std::this_thread::yield();
      }
      ObPluginLease lease;
      const int ret = registry.acquire("com.seekdb.concurrent.value", 1, 0, lease);
      if (OB_SUCCESS == ret) {
        acquired.fetch_add(1);
      } else if (OB_ENTRY_NOT_EXIST == ret) {
        missing.fetch_add(1);
      } else {
        unexpected.fetch_add(1);
      }
      attempted.fetch_add(1);
      while (lease.is_valid() && !release.load()) {
        std::this_thread::yield();
      }
    });
  }
  while (THREAD_COUNT != ready.load()) {
    std::this_thread::yield();
  }
  go.store(true);
  EXPECT_EQ(OB_SUCCESS, registry.quiesce(owner));
  while (THREAD_COUNT != attempted.load()) {
    std::this_thread::yield();
  }

  EXPECT_EQ(0, unexpected.load());
  EXPECT_EQ(THREAD_COUNT, acquired.load() + missing.load());
  if (acquired.load() > 0) {
    EXPECT_EQ(OB_TIMEOUT, owner->wait_for_drain(0));
  }
  release.store(true);
  for (std::thread &thread : threads) {
    thread.join();
  }
  EXPECT_EQ(OB_SUCCESS, owner->wait_for_drain(1000000));
  EXPECT_EQ(OB_SUCCESS, registry.mark_stopped(owner));
}

} // namespace plugin
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
